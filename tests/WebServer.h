#pragma once
#include "Arduino.h"
#include <functional>
class WebServer {
public:
    WebServer(int=80){}
    void on(const char*,std::function<void()>){}
    template<class M> void on(const char*,M,std::function<void()>){}
    void begin(){}
    void handleClient(){}
    void stop(){}
    void send(int,const char*,const String&){}
    String arg(const char*){ return ""; }
    bool   hasArg(const char*){ return false; }
    void   onNotFound(std::function<void()>){}
};
