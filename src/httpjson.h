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

#endif // HTTPJSON_H
