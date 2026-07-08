// Mock HTTPClient for unit tests.
// Tests register expected URL → response pairs via MockHTTP::set().
//
// The body is served through MockBodyStream, which mimics an Arduino
// WiFiClient (available()/connected()/readBytes()) instead of a std::istream
// — so the SAME httpjson code path that runs on the device (the
// available()/connected() wait loop in fillFrom) is what unit tests
// exercise. Fault injection:
//   setBursts(url, n)       — deliver the body n bytes at a time, with
//                             empty-available() gaps between bursts (models
//                             TLS records trickling in)
//   setTruncateOnce(url, n) — the FIRST GET of url serves only n bytes and
//                             then reports the connection closed (models the
//                             mid-body CDN stalls seen on hardware);
//                             subsequent GETs serve the full body
//   setChunked(url)         — getSize() reports -1 (no Content-Length)
#pragma once
#include "Arduino.h"
#include "WiFiClientSecure.h"
#include <map>
#include <set>
#include <string>
#include <cstring>

namespace MockHTTP {
    inline std::map<std::string, std::string> responses;
    inline std::map<std::string, int>         statusCodes;
    inline std::set<std::string>              chunkedUrls;
    inline std::map<std::string, size_t>      burstSizes;
    inline std::map<std::string, size_t>      truncateOnceAt;
    inline std::map<std::string, int>         getCounts;   // per-URL GET() calls

    inline void set(const char* url, const char* body, int code = 200) {
        responses[url]   = body;
        statusCodes[url] = code;
    }
    inline void setChunked(const char* url)                { chunkedUrls.insert(url); }
    inline void setBursts(const char* url, size_t n)       { burstSizes[url] = n; }
    inline void setTruncateOnce(const char* url, size_t n) { truncateOnceAt[url] = n; }
    inline void clear() {
        responses.clear(); statusCodes.clear(); chunkedUrls.clear();
        burstSizes.clear(); truncateOnceAt.clear(); getCounts.clear();
    }
}

// Arduino-WiFiClient-shaped body stream (deliberately NOT a std::istream).
class MockBodyStream {
    std::string _body;
    size_t _pos = 0;
    size_t _burst = SIZE_MAX;   // bytes exposed per available() window
    bool   _closedEarly = false;
    int    _gap = 0;            // polls of available()==0 between bursts
public:
    void start(const std::string& body, size_t burst, bool closedEarly) {
        _body = body; _pos = 0; _closedEarly = closedEarly;
        _burst = burst ? burst : SIZE_MAX;
        _gap = 0;
    }
    int available() {
        if (_pos >= _body.size()) return 0;
        if (_gap > 0) { _gap--; return 0; }   // simulate a between-records gap
        size_t rem = _body.size() - _pos;
        return (int)(rem < _burst ? rem : _burst);
    }
    // connected() stays true while data remains; after the body is drained a
    // truncated stream reports "closed" (early FIN) and a complete one does
    // too (normal HTTP/1.0 connection-close) — both are EOF for fillFrom.
    bool connected() { return _pos < _body.size(); }
    size_t readBytes(char* buf, size_t n) {
        size_t avail = (size_t)available();
        size_t take = n < avail ? n : avail;
        if (take == 0) return 0;
        memcpy(buf, _body.data() + _pos, take);
        _pos += take;
        if (_burst != SIZE_MAX && (_pos % _burst) == 0) _gap = 3;
        return take;
    }
};

class HTTPClient {
    std::string    _url;
    std::string    _body;
    int            _status = 200;
    MockBodyStream _stream;
public:
    bool begin(WiFiClientSecure&, const String& url) { _url = url.c_str(); return true; }
    bool begin(const String& url) { _url = url.c_str(); return true; }
    void setTimeout(int) {}
    void useHTTP10(bool) {}
    void addHeader(const String&, const String&) {}

    int GET() {
        auto it = MockHTTP::responses.find(_url);
        if (it != MockHTTP::responses.end()) {
            _body   = it->second;
            _status = MockHTTP::statusCodes.count(_url) ? MockHTTP::statusCodes[_url] : 200;
        } else {
            _body   = "";
            _status = 404;
        }
        int attempt = MockHTTP::getCounts[_url]++;

        std::string served = _body;
        bool truncated = false;
        auto tr = MockHTTP::truncateOnceAt.find(_url);
        if (tr != MockHTTP::truncateOnceAt.end() && attempt == 0 &&
            served.size() > tr->second) {
            served.resize(tr->second);
            truncated = true;
        }
        size_t burst = 0;
        auto b = MockHTTP::burstSizes.find(_url);
        if (b != MockHTTP::burstSizes.end()) burst = b->second;

        _stream.start(served, burst, truncated);
        return _status;
    }

    int getSize() const {
        return MockHTTP::chunkedUrls.count(_url) ? -1 : (int)_body.size();
    }
    String getString()          { return String(_body.c_str()); }
    MockBodyStream& getStream() { return _stream; }
    void end() {}
};
