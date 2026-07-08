// File-backed Preferences stub — stores to sim_prefs_<namespace>.txt
// Format: one "key=value" per line (values are URL-encoded for safety).
#pragma once
#include "Arduino.h"
#include <fstream>
#include <map>
#include <string>

class Preferences {
    std::string _ns;
    bool        _ro = false;
    std::map<std::string, std::string> _data;

    std::string _path() const { return "sim_prefs_" + _ns + ".txt"; }

    void _load() {
        _data.clear();
        std::ifstream f(_path());
        std::string line;
        while (std::getline(f, line)) {
            auto eq = line.find('=');
            if (eq == std::string::npos) continue;
            _data[line.substr(0, eq)] = line.substr(eq + 1);
        }
    }

    void _save() const {
        if (_ro) return;
        std::ofstream f(_path(), std::ios::trunc);
        for (auto& kv : _data) f << kv.first << "=" << kv.second << "\n";
    }

public:
    bool begin(const char* name, bool readOnly = false) {
        _ns = name; _ro = readOnly; _load(); return true;
    }
    void end() { _save(); }

    // String — returns bytes written like the real Preferences (0 = failure)
    size_t putString(const char* key, const String& val) {
        _data[key] = val.c_str(); return val.length();
    }
    String getString(const char* key, const String& def = "") const {
        auto it = _data.find(key);
        return it != _data.end() ? String(it->second.c_str()) : def;
    }

    // ULong
    bool putULong(const char* key, unsigned long val) {
        _data[key] = std::to_string(val); return true;
    }
    unsigned long getULong(const char* key, unsigned long def = 0) const {
        auto it = _data.find(key);
        return it != _data.end() ? std::stoul(it->second) : def;
    }

    // Int
    bool putInt(const char* key, int val) {
        _data[key] = std::to_string(val); return true;
    }
    int getInt(const char* key, int def = 0) const {
        auto it = _data.find(key);
        return it != _data.end() ? std::stoi(it->second) : def;
    }

    // Bool
    bool putBool(const char* key, bool val) {
        _data[key] = val ? "1" : "0"; return true;
    }
    bool getBool(const char* key, bool def = false) const {
        auto it = _data.find(key);
        return it != _data.end() ? (it->second == "1") : def;
    }

    bool isKey(const char* key) const { return _data.count(key) > 0; }
    bool remove(const char* key) { return _data.erase(key) > 0; }
    void clear() { _data.clear(); }
};
