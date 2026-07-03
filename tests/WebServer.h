#pragma once
#include "Arduino.h"
#include <functional>

// Arduino WebServer HTTP method enum (mirrors sim/WebServer.h)
enum HTTPMethod { HTTP_ANY = 0, HTTP_GET, HTTP_POST, HTTP_PUT, HTTP_DELETE, HTTP_PATCH };

class WebServer {
public:
    WebServer(int=80){}
    void on(const char*,std::function<void()>){}
    template<class M> void on(const char*,M,std::function<void()>){}
    void begin(){}
    void handleClient(){}
    void stop(){}
    void close(){}
    void send(int,const char*,const String&){}
    // PROGMEM is a no-op in this build (see sim/Arduino.h's PROGMEM/pgm_read_byte
    // no-ops), so content is just a normal null-terminated buffer here.
    void send_P(int, const char*, const char* content, size_t=0) { (void)content; }
    String arg(const char*){ return ""; }
    bool   hasArg(const char*){ return false; }
    void   onNotFound(std::function<void()>){}
};
