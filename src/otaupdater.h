#ifndef OTAUPDATER_H
#define OTAUPDATER_H

#include <Arduino.h>
#include <ArduinoJson.h>

enum class OtaResult {
  Success,
  ErrorDownloadFailed,
  ErrorFlashFailed,
  ErrorNotEnoughSpace,
  ErrorNotSupported   // sim stub only — no real hardware to flash
};
using OtaProgressFn = void (*)(size_t written, size_t total);

// Pure extraction of a GitHub "releases/latest" API response into a version
// tag + the ".bin" asset's download URL (picked by name suffix, not asset
// index — a release may attach other files too). Header-only so it's
// testable without pulling in OtaUpdater's WiFi/Update.h dependencies.
// Returns false if there's no tag_name or no ".bin" asset.
inline bool extractLatestRelease(JsonVariantConst releaseDoc,
                                  String& outVersion, String& outAssetUrl) {
  const char* tag = releaseDoc["tag_name"] | (const char*)nullptr;
  if (!tag || tag[0] == '\0') return false;

  JsonArrayConst assets = releaseDoc["assets"].as<JsonArrayConst>();
  for (JsonObjectConst asset : assets) {
    const char* name = asset["name"] | "";
    const char* url  = asset["browser_download_url"] | (const char*)nullptr;
    if (url && String(name).endsWith(".bin")) {
      outVersion  = tag;
      outAssetUrl = url;
      return true;
    }
  }
  return false;
}

class OtaUpdater {
public:
  // Checks GitHub Releases for a newer tag than FIRMWARE_VERSION. Returns
  // true (with outVersion/outAssetUrl populated) only when a .bin asset for
  // a *different* version is found; false for "already up to date" and for
  // any fetch/parse failure alike (matching this codebase's existing
  // httpGetJson-style bool-return convention — callers show a generic "no
  // update" message either way).
  bool checkForUpdate(String& outVersion, String& outAssetUrl);

  // Downloads assetUrl and flashes it via Update.h, reporting progress via
  // onProgress. Never touches the currently-running image unless the
  // download completes and verifies fully.
  OtaResult performUpdate(const String& assetUrl, OtaProgressFn onProgress = nullptr);

  // Rollback / boot-confirmation — see appstate.cpp's tickWaitTimeCycle().
  static bool isPendingConfirmation();
  static void markBootSuccessful();

private:
  // Follows exactly one redirect (GitHub's asset download 302s to a CDN
  // host) and returns the final URL, or "" on failure. Deliberately local/
  // manual rather than enabling HTTPClient's global follow-redirects, so
  // QueueApi's existing HTTPS behavior is untouched.
  String resolveRedirect(const String& url);
};

#endif // OTAUPDATER_H
