// In-memory Preferences mock for unit tests. No file I/O.
// Storage is shared per namespace across ALL instances — matching real NVS
// semantics (two Preferences handles opened on the same namespace see the
// same keys). Tests that need a clean slate call Preferences::resetMock().
#pragma once
#include "Arduino.h"
#include <map>
#include <string>

class Preferences {
    static std::map<std::string, std::map<std::string, std::string>>& store() {
        static std::map<std::string, std::map<std::string, std::string>> s;
        return s;
    }
    std::map<std::string, std::string>* _ns = nullptr;
public:
    static void resetMock() { store().clear(); }

    bool begin(const char* ns, bool = false) { _ns = &store()[ns]; return true; }
    void end() {}

    // Real Preferences returns the number of bytes written (0 = failure).
    size_t putString(const char* k, const String& v) {
        (*_ns)[k] = v.c_str();
        return v.length();
    }
    String getString(const char* k, const String& def="") const {
        if (!_ns) return def;
        auto it=_ns->find(k); return it!=_ns->end()?String(it->second.c_str()):def;
    }
    size_t putULong(const char* k, unsigned long v) { (*_ns)[k]=std::to_string(v); return 4; }
    unsigned long getULong(const char* k, unsigned long def=0) const {
        auto it=_ns->find(k); return it!=_ns->end()?std::stoul(it->second):def;
    }
    size_t putInt(const char* k, int v)    { (*_ns)[k]=std::to_string(v); return 4; }
    int  getInt(const char* k, int def=0) const {
        auto it=_ns->find(k); return it!=_ns->end()?std::stoi(it->second):def;
    }
    size_t putBool(const char* k, bool v)  { (*_ns)[k]=v?"1":"0"; return 1; }
    bool getBool(const char* k, bool def=false) const {
        auto it=_ns->find(k); return it!=_ns->end()?(it->second=="1"):def;
    }
    bool isKey(const char* k) const { return _ns->count(k)>0; }
    bool remove(const char* k)      { return _ns->erase(k)>0; }
    void clear()                    { if (_ns) _ns->clear(); }
};
