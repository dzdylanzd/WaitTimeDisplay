// Mock HTTPClient for unit tests.
// Tests register expected URL → response pairs via MockHTTP::set().
#pragma once
#include "Arduino.h"
#include "WiFiClientSecure.h"
#include <map>
#include <string>

namespace MockHTTP {
    inline std::map<std::string, std::string> responses;
    inline std::map<std::string, int>         statusCodes;

    inline void set(const char* url, const char* body, int code = 200) {
        responses[url]   = body;
        statusCodes[url] = code;
    }
    inline void clear() { responses.clear(); statusCodes.clear(); }
}

class HTTPClient {
    std::string _url;
    std::string _body;
    int         _status = 200;
public:
    bool begin(WiFiClientSecure&, const String& url) { _url = url.c_str(); return true; }
    bool begin(const String& url) { _url = url.c_str(); return true; }
    void setTimeout(int) {}

    int GET() {
        auto it = MockHTTP::responses.find(_url);
        if (it != MockHTTP::responses.end()) {
            _body   = it->second;
            _status = MockHTTP::statusCodes.count(_url) ? MockHTTP::statusCodes[_url] : 200;
        } else {
            _body   = "";
            _status = 404;
        }
        return _status;
    }

    int    getSize() const { return (int)_body.size(); }
    String getString()    { return String(_body.c_str()); }
    void   end() {}
};
