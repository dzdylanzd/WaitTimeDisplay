#include "otaupdater.h"
#include "config.h"
#include "httpjson.h"
#include "versioncompare.h"
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <Update.h>
#include <esp_ota_ops.h>
#include <Preferences.h>

#if __has_include(<esp_task_wdt.h>)
#include <esp_task_wdt.h>
static inline void feedWdtIfSubscribed() {
  if (esp_task_wdt_status(NULL) == ESP_OK) esp_task_wdt_reset();
}
#define FEED_WDT() feedWdtIfSubscribed()
#else
#define FEED_WDT() ((void)0)
#endif

static const char* kReleasesUrl =
    "https://api.github.com/repos/dzdylanzd/WaitTimeDisplay/releases/latest";
static const char* kUserAgent = "QueueWatch-ESP32";

bool OtaUpdater::checkForUpdate(String& outVersion, String& outAssetUrl) {
  // Only the fields extractLatestRelease() actually reads, to keep the doc
  // small (matches QueueApi::fetchParksDoc's filtered-fetch approach).
  StaticJsonDocument<192> filter;
  filter["tag_name"] = true;
  JsonObject fAsset = filter["assets"][0].to<JsonObject>();
  fAsset["name"] = true;
  fAsset["browser_download_url"] = true;

  DynamicJsonDocument doc(4096);
  if (!httpGetJson(kReleasesUrl, doc, &filter, kUserAgent)) return false;

  String version, assetUrl;
  if (!extractLatestRelease(doc.as<JsonVariantConst>(), version, assetUrl)) return false;

  // Only offer an update for a strictly newer version — never an equal build
  // (regardless of 'v'-prefix formatting) nor an older one, which a
  // mis-tagged "latest" release on GitHub could otherwise trigger.
  if (!isNewerVersion(FIRMWARE_VERSION, version)) return false;

  outVersion  = version;
  outAssetUrl = assetUrl;
  return true;
}

String OtaUpdater::resolveRedirect(const String& url) {
  WiFiClientSecure client;
  client.setInsecure();
  HTTPClient http;
  if (!http.begin(client, url)) { http.end(); return ""; }
  http.setTimeout(HTTP_TIMEOUT_MS);
  http.addHeader("User-Agent", kUserAgent);
  // HTTPClient::header() only returns headers explicitly requested here —
  // without this, it silently returns "" for every header, including a
  // real Location on a real redirect (this was a real bug: every redirect
  // response looked identical to "no Location sent").
  static const char* kHeaderKeys[] = {"Location"};
  http.collectHeaders(kHeaderKeys, 1);

  int code = http.GET();
  String result;
  if (code == 301 || code == 302) {
    result = http.header("Location");
  } else if (code == 200) {
    result = url;   // no redirect; caller re-fetches the same URL to stream it
  }
  http.end();
  return result;
}

OtaResult OtaUpdater::performUpdate(const String& assetUrl, OtaProgressFn onProgress) {
#ifndef SIMULATION
  if (WiFi.status() != WL_CONNECTED) return OtaResult::ErrorDownloadFailed;
#endif

  String finalUrl = resolveRedirect(assetUrl);
  if (finalUrl.length() == 0) return OtaResult::ErrorDownloadFailed;

  WiFiClientSecure client;
  client.setInsecure();
  HTTPClient http;
  if (!http.begin(client, finalUrl)) { http.end(); return OtaResult::ErrorDownloadFailed; }
  http.setTimeout(HTTP_TIMEOUT_MS);
  http.addHeader("User-Agent", kUserAgent);

  int code = http.GET();
  if (code != 200) { http.end(); return OtaResult::ErrorDownloadFailed; }

  int contentLength = http.getSize();
  if (contentLength <= 0) { http.end(); return OtaResult::ErrorDownloadFailed; }

  if ((size_t)contentLength > ESP.getFreeSketchSpace()) {
    http.end();
    return OtaResult::ErrorNotEnoughSpace;
  }

  if (!Update.begin((size_t)contentLength)) {
    http.end();
    return OtaResult::ErrorFlashFailed;
  }

  WiFiClient* stream = http.getStreamPtr();
  uint8_t buf[1024];
  size_t written = 0;
  while (http.connected() && written < (size_t)contentLength) {
    int avail = stream->available();
    if (avail <= 0) { delay(10); FEED_WDT(); continue; }
    size_t toRead = (size_t)avail > sizeof(buf) ? sizeof(buf) : (size_t)avail;
    size_t n = stream->readBytes(buf, toRead);
    if (n == 0) break;
    if (Update.write(buf, n) != n) {
      http.end();
      Update.abort();
      return OtaResult::ErrorFlashFailed;
    }
    written += n;
    if (onProgress) onProgress(written, (size_t)contentLength);
    FEED_WDT();
  }
  http.end();

  if (written != (size_t)contentLength || !Update.end(true) || Update.hasError()) {
    return OtaResult::ErrorFlashFailed;
  }

  // Mark this boot as pending confirmation. AppStateManager clears this
  // (via markBootSuccessful()) only after several consecutive successful
  // wait-time fetches post-reboot, so a new image that boots but is
  // otherwise broken (e.g. WiFi/API logic regressed) still gets caught,
  // not just a crash-loop.
  Preferences prefs;
  prefs.begin(NVS_NAMESPACE, false);
  prefs.putBool("ota_pending", true);
  prefs.end();

  return OtaResult::Success;
}

bool OtaUpdater::isPendingConfirmation() {
  Preferences prefs;
  prefs.begin(NVS_NAMESPACE, true);
  bool pending = prefs.getBool("ota_pending", false);
  prefs.end();
  return pending;
}

void OtaUpdater::markBootSuccessful() {
  Preferences prefs;
  prefs.begin(NVS_NAMESPACE, false);
  prefs.putBool("ota_pending", false);
  prefs.end();
  esp_ota_mark_app_valid_cancel_rollback();
}
