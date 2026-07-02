// In-memory Preferences mock for unit tests. No file I/O.
#pragma once
#include "Arduino.h"
#include <map>
#include <string>

class Preferences {
    std::map<std::string, std::string> _data;
public:
    bool begin(const char*, bool = false) { return true; }
    void end() {}

    bool putString(const char* k, const String& v) { _data[k]=v.c_str(); return true; }
    String getString(const char* k, const String& def="") const {
        auto it=_data.find(k); return it!=_data.end()?String(it->second.c_str()):def;
    }
    bool putULong(const char* k, unsigned long v) { _data[k]=std::to_string(v); return true; }
    unsigned long getULong(const char* k, unsigned long def=0) const {
        auto it=_data.find(k); return it!=_data.end()?std::stoul(it->second):def;
    }
    bool putInt(const char* k, int v)    { _data[k]=std::to_string(v); return true; }
    int  getInt(const char* k, int def=0) const {
        auto it=_data.find(k); return it!=_data.end()?std::stoi(it->second):def;
    }
    bool putBool(const char* k, bool v)  { _data[k]=v?"1":"0"; return true; }
    bool getBool(const char* k, bool def=false) const {
        auto it=_data.find(k); return it!=_data.end()?(it->second=="1"):def;
    }
    bool isKey(const char* k) const { return _data.count(k)>0; }
    bool remove(const char* k)      { return _data.erase(k)>0; }
    void clear()                    { _data.clear(); }
};
