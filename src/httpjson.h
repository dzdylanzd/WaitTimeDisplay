#ifndef HTTPJSON_H
#define HTTPJSON_H

#include <Arduino.h>
#include <ArduinoJson.h>

// Shared HTTPS-GET + JSON-parse helper used by QueueApi (queue-times.com)
// and OtaUpdater (GitHub Releases). Retries up to HTTP_RETRY_MAX times,
// rejects unbounded/oversized bodies (HTTP_MAX_RESPONSE_SIZE), feeds the
// task watchdog around each blocking step, and optionally filters the
// parse (and sends a User-Agent header, required by some APIs like
// GitHub's) to keep memory use down.
bool httpGetJson(const String& url, DynamicJsonDocument& doc,
                 JsonDocument* filter = nullptr,
                 const char* userAgent = nullptr);

#include <functional>

// Streams a top-level JSON array ("arrayKey": [ {...}, {...} ]) element by
// element: each element is parsed into elemDoc (optionally through
// elemFilter) and handed to onElement. Memory use is bounded by elemDoc's
// capacity no matter how large the body is — required for /live payloads of
// show-heavy parks, whose filtered content alone exceeds any doc the ESP32
// can afford next to a TLS session.
//
// onElement returns false to stop early (still counts as success). onBegin
// runs before the first element of every attempt — retries restart the
// element sequence, so callers must reset their accumulation state there.
bool httpGetJsonArray(const String& url, const char* arrayKey,
                      DynamicJsonDocument& elemDoc, JsonDocument* elemFilter,
                      const std::function<void()>& onBegin,
                      const std::function<bool(JsonObject)>& onElement,
                      const char* userAgent = nullptr);

#endif // HTTPJSON_H
