#include "httpjson.h"
#include "config.h"
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>

#if __has_include(<esp_task_wdt.h>)
#include <esp_task_wdt.h>
// The Arduino loopTask is not subscribed to the task watchdog on the C6,
// so a blind esp_task_wdt_reset() logs "task_wdt: task not found" on every
// call. Only feed it when the current task is actually subscribed.
static inline void feedWdtIfSubscribed() {
  if (esp_task_wdt_status(NULL) == ESP_OK) esp_task_wdt_reset();
}
#define FEED_WDT() feedWdtIfSubscribed()
#else
#define FEED_WDT() ((void)0)
#endif

bool httpGetJson(const String& url, DynamicJsonDocument& doc,
                  JsonDocument* filter, const char* userAgent) {
  // On device, WiFi must be connected before making any HTTP call.
  // In the sim (SIMULATION=1) we skip this check — internet is always available.
#ifndef SIMULATION
  if (WiFi.status() != WL_CONNECTED) return false;
#endif

  for (uint8_t attempt = 0; attempt <= HTTP_RETRY_MAX; attempt++) {
    WiFiClientSecure client;
    client.setInsecure();
    HTTPClient http;

    if (!http.begin(client, url)) {
      http.end();
      return false;
    }
    http.setTimeout(HTTP_TIMEOUT_MS);
    if (userAgent) http.addHeader("User-Agent", userAgent);

    FEED_WDT();
    int httpCode = http.GET();
    FEED_WDT();

    if (httpCode == 200) {
      // getSize() returns -1 when the server didn't send Content-Length
      // (e.g. chunked encoding) — treat that as unknown/untrusted rather
      // than letting an unbounded body through the size gate.
      int size = http.getSize();
      if (size < 0 || size > (int)HTTP_MAX_RESPONSE_SIZE) {
        Serial.printf("HTTP response rejected: %d bytes (max %u)\n",
                      size, (unsigned)HTTP_MAX_RESPONSE_SIZE);
        http.end();
        return false;
      }

      String payload = http.getString();
      http.end();

      FEED_WDT();
      doc.clear();
      DeserializationError error = filter
        ? deserializeJson(doc, payload, DeserializationOption::Filter(*filter))
        : deserializeJson(doc, payload);
      if (error) {
        Serial.printf("JSON parse error: %s\n", error.c_str());
        return false;
      }
      return true;
    }

    Serial.printf("HTTP %s attempt %u failed: %d\n",
                  url.c_str(), attempt, httpCode);
    http.end();
    delay(300);
  }

  return false;
}
