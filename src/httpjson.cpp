#include "httpjson.h"
#include "config.h"
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <type_traits>

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

// --- buffered stream reads over both body-stream types -------------------
// Device: HTTPClient::getStream() is an Arduino Stream. Sim/tests: the stubs
// hand back a std::istream. SFINAE picks the right overload; only the one
// matching the build's stream type is ever instantiated. Reading the TLS
// stream one byte at a time is brutally slow on the ESP32 (every byte is an
// mbedtls_ssl_read) and risks mid-body timeouts, so pull whole chunks.
template <typename S>
static auto fillFrom(S& in, char* buf, size_t cap) -> decltype(in.get(), size_t()) {
  in.read(buf, cap);                 // std::istream: blocks until cap or EOF
  auto n = in.gcount();
  return n < 0 ? 0 : (size_t)n;
}
// Arduino WiFiClient(Secure): wait on available()/connected() exactly like
// HTTPClient::writeToStream() does. Stream::readBytes' per-byte timedRead
// gives up during the small gaps between TLS records (verified on device:
// direct getStream() reads died after ~2-5 KB of a 38 KB body while the
// buffered getString() path always got everything), so the socket must be
// polled with available() — which also drives mbedtls record processing.
template <typename S>
static auto fillFrom(S& in, char* buf, size_t cap)
    -> decltype(in.connected(), in.readBytes((char*)nullptr, 0), size_t()) {
  unsigned long start = millis();
  while (millis() - start < HTTP_STALL_TIMEOUT_MS) {
    int avail = in.available();
    if (avail > 0) {
      size_t want = ((size_t)avail < cap) ? (size_t)avail : cap;
      size_t n = in.readBytes(buf, want);
      if (n > 0) return n;
    } else if (!in.connected()) {
      return 0;                      // clean end: closed and drained
    } else {
      delay(1);                      // record in flight — let WiFi run
    }
  }
  return 0;                          // no data for HTTP_TIMEOUT_MS
}

// Custom ArduinoJson reader: buffered, with one byte of pushback so the
// array scanner can look at a delimiter and still hand a complete element
// to the parser.
template <typename S>
struct PushbackReader {
  S&     in;
  int    pending = -1;
  size_t consumed = 0;   // total bytes taken from the body (diagnostics)
  char   buf[512];
  size_t bufLen = 0, bufPos = 0;

  int read() {
    if (pending >= 0) { int c = pending; pending = -1; return c; }
    if (bufPos >= bufLen) {
      bufLen = fillFrom(in, buf, sizeof(buf));
      bufPos = 0;
      if (bufLen == 0) return -1;
      consumed += bufLen;
    }
    return (unsigned char)buf[bufPos++];
  }
  size_t readBytes(char* dst, size_t n) {
    size_t i = 0;
    for (; i < n; i++) {
      int c = read();
      if (c < 0) break;
      dst[i] = (char)c;
    }
    return i;
  }
};

// Advance the reader just past the '[' of `"arrayKey": [`.
// The naive substring match could in principle hit the key inside an earlier
// string value, but every payload this is used on has the array as its first
// (or only) top-level member.
template <typename S>
static bool scanToArrayStart(PushbackReader<S>& in, const char* key) {
  String patStr = String("\"") + key + "\"";
  const char* pat = patStr.c_str();
  size_t patLen = patStr.length();
  size_t m = 0;
  int c;
  while ((c = in.read()) >= 0) {
    if ((char)c == pat[m]) {
      if (++m == patLen) break;
    } else {
      m = ((char)c == pat[0]) ? 1 : 0;
    }
  }
  if (c < 0) return false;
  while ((c = in.read()) >= 0) {
    if (c == '[') return true;
    if (c != ':' && c != ' ' && c != '\t' && c != '\r' && c != '\n') return false;
  }
  return false;
}

template <typename S>
static int nextNonSpace(PushbackReader<S>& in) {
  int c;
  do {
    c = in.read();
  } while (c == ' ' || c == '\t' || c == '\r' || c == '\n');
  return c;
}

bool httpGetJsonArray(const String& url, const char* arrayKey,
                      DynamicJsonDocument& elemDoc, JsonDocument* elemFilter,
                      const std::function<void()>& onBegin,
                      const std::function<bool(JsonObject)>& onElement,
                      const char* userAgent) {
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
    http.useHTTP10(true);  // no chunk framing on the raw stream (see above)
    if (userAgent) http.addHeader("User-Agent", userAgent);

    FEED_WDT();
    int httpCode = http.GET();
    FEED_WDT();

    if (httpCode == 200) {
      int size = http.getSize();
      if (size > (int)HTTP_MAX_RESPONSE_SIZE) {
        Serial.printf("HTTP response rejected: %d bytes (max %u)\n",
                      size, (unsigned)HTTP_MAX_RESPONSE_SIZE);
        http.end();
        return false;
      }

      auto& body = http.getStream();
      PushbackReader<typename std::remove_reference<decltype(body)>::type>
          reader{body};

      bool parseFailed = false;
      if (scanToArrayStart(reader, arrayKey)) {
        onBegin();
        int c = nextNonSpace(reader);
        while (c == '{') {
          reader.pending = c;  // give the '{' back to the parser
          FEED_WDT();
          DeserializationError err = elemFilter
            ? deserializeJson(elemDoc, reader,
                              DeserializationOption::Filter(*elemFilter))
            : deserializeJson(elemDoc, reader);
          if (err) {
            Serial.printf("JSON element parse error: %s (size %d, read %u, heap %u)\n",
                          err.c_str(), size, (unsigned)reader.consumed,
                          (unsigned)ESP.getFreeHeap());
            parseFailed = true;
            break;
          }
          if (!onElement(elemDoc.as<JsonObject>())) { c = ']'; break; }
          c = nextNonSpace(reader);
          if (c == ',') c = nextNonSpace(reader);
        }
        http.end();
        FEED_WDT();
        if (!parseFailed && c == ']') return true;
        // Mid-body stall or truncated stream: a fresh connection usually
        // succeeds (CDN hiccup), so spend the remaining attempts on it
        // instead of failing the whole fetch on one bad read.
        if (attempt < HTTP_RETRY_MAX) { delay(300); continue; }
        return false;
      }

      Serial.printf("JSON array \"%s\" not found in %s\n",
                    arrayKey, url.c_str());
      http.end();
      if (attempt < HTTP_RETRY_MAX) { delay(300); continue; }
      return false;
    }

    Serial.printf("HTTP %s attempt %u failed: %d\n",
                  url.c_str(), attempt, httpCode);
    http.end();
    if (attempt < HTTP_RETRY_MAX) delay(300);
  }

  return false;
}

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
    // HTTP/1.0 forbids chunked transfer-encoding, so getStream() below hands
    // the parser a raw JSON body (with 1.1, chunk framing would corrupt it).
    // Both queue-times/themeparks.wiki (Cloudflare) and GitHub accept 1.0.
    http.useHTTP10(true);
    if (userAgent) http.addHeader("User-Agent", userAgent);

    FEED_WDT();
    int httpCode = http.GET();
    FEED_WDT();

    if (httpCode == 200) {
      int size = http.getSize();
      if (size > (int)HTTP_MAX_RESPONSE_SIZE) {
        Serial.printf("HTTP response rejected: %d bytes (max %u)\n",
                      size, (unsigned)HTTP_MAX_RESPONSE_SIZE);
        http.end();
        return false;
      }

      FEED_WDT();
      doc.clear();
      // Parse straight off the socket — no body copy, so a 50 KB response
      // costs only what survives the filter. size < 0 (no Content-Length,
      // HTTP/1.0 body ends at connection close) is fine: the parse is
      // bounded by the doc's capacity and stops at the document's end.
      // DIAG: last attempt buffers via getString() instead — isolates
      // whether the mid-body stall is our reader or the transport.
      DeserializationError error = DeserializationError::Ok;
      if (attempt < HTTP_RETRY_MAX) {
        auto& body = http.getStream();
        PushbackReader<typename std::remove_reference<decltype(body)>::type>
            reader{body};
        error = filter
          ? deserializeJson(doc, reader, DeserializationOption::Filter(*filter))
          : deserializeJson(doc, reader);
        if (error) {
          Serial.printf("stream parse: %s (size %d, read %u, heap %u)\n",
                        error.c_str(), size, (unsigned)reader.consumed,
                        (unsigned)ESP.getFreeHeap());
        }
      } else {
        String payload = http.getString();
        Serial.printf("buffered fallback: got %u bytes (heap %u)\n",
                      (unsigned)payload.length(), (unsigned)ESP.getFreeHeap());
        error = filter
          ? deserializeJson(doc, payload, DeserializationOption::Filter(*filter))
          : deserializeJson(doc, payload);
      }

      FEED_WDT();
      http.end();
      if (error) {
        Serial.printf("JSON parse error: %s\n", error.c_str());
        if (attempt < HTTP_RETRY_MAX) { delay(300); continue; }
        return false;
      }
      return true;
    }

    Serial.printf("HTTP %s attempt %u failed: %d\n",
                  url.c_str(), attempt, httpCode);
    http.end();
    // Back off before the next try, but not after the final attempt — that
    // delay would just stall the caller's return without buying a retry.
    if (attempt < HTTP_RETRY_MAX) delay(300);
  }

  return false;
}
