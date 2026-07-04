#pragma once
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <cstdarg>
#include <string>
#include <vector>
#include <chrono>
#include <thread>

using byte = uint8_t;

// Flash memory macros — no-ops on desktop
#define PROGMEM
#define pgm_read_byte(addr)  (*((const uint8_t*)(addr)))
#define pgm_read_word(addr)  (*((const uint16_t*)(addr)))
#define F(s) (s)

// ── ESP core stub ─────────────────────────────────────────────────────────────
// ESP.restart() reboots the MCU on the device; the sim equivalent is exiting
// the process (relaunch it to "boot" again).
struct EspClass {
    void restart() {
        printf("[sim] ESP.restart() — exiting (relaunch to reboot)\n");
        std::exit(0);
    }
    // Desktop has effectively unlimited heap; report "plenty free" so the
    // firmware's low-heap self-reboot guard never fires in the sim.
    uint32_t getFreeHeap() { return 250000; }
};
inline EspClass ESP;

// ── String ────────────────────────────────────────────────────────────────────
class String {
    std::string _s;
public:
    String() {}
    String(const char* s)       : _s(s ? s : "") {}
    String(int n)               : _s(std::to_string(n)) {}
    String(long n)              : _s(std::to_string(n)) {}
    String(unsigned int n)      : _s(std::to_string(n)) {}
    String(unsigned long n)     : _s(std::to_string(n)) {}
    String(float n, int dec=2)  { char b[32]; snprintf(b,sizeof(b),"%.*f",dec,n); _s=b; }
    String(double n, int dec=2) { char b[32]; snprintf(b,sizeof(b),"%.*f",dec,n); _s=b; }
    explicit String(const std::string& s) : _s(s) {}

    const char* c_str()   const { return _s.c_str(); }
    size_t      length()  const { return _s.length(); }
    bool        isEmpty() const { return _s.empty(); }
    void        reserve(size_t n) { _s.reserve(n); }
    char        charAt(size_t i) const { return _s[i]; }
    int         toInt() const { return std::stoi(_s.empty() ? "0" : _s); }
    float       toFloat() const { return std::stof(_s.empty() ? "0" : _s); }

    String& operator=(const String& o) { _s = o._s; return *this; }
    String& operator=(const char*   s) { _s = s ? s : ""; return *this; }

    bool operator==(const String& o)  const { return _s == o._s; }
    bool operator==(const char*   s)  const { return _s == (s ? s : ""); }
    bool operator!=(const String& o)  const { return _s != o._s; }
    bool operator!=(const char*   s)  const { return _s != (s ? s : ""); }
    bool operator< (const String& o)  const { return _s  < o._s; }

    String  operator+(const String& o)      const { return String(std::string(_s + o._s)); }
    String  operator+(const char*   s)      const { return String(std::string(_s + (s ? s : ""))); }
    String  operator+(int n)                const { return String(std::string(_s + std::to_string(n))); }
    String  operator+(unsigned int n)       const { return String(std::string(_s + std::to_string(n))); }
    String  operator+(long n)               const { return String(std::string(_s + std::to_string(n))); }
    String& operator+=(const String& o) { _s += o._s; return *this; }
    String& operator+=(const char*   s) { _s += s ? s : ""; return *this; }
    String& operator+=(char c)          { _s += c; return *this; }
    String& operator+=(int n)           { _s += std::to_string(n); return *this; }

    String substring(size_t from) const { return String(std::string(_s.substr(from))); }
    String substring(size_t from, size_t to) const { return String(std::string(_s.substr(from, to-from))); }

    int    indexOf(char c) const { auto p=_s.find(c); return p==std::string::npos?-1:(int)p; }
    int    indexOf(const String& s) const { auto p=_s.find(s._s); return p==std::string::npos?-1:(int)p; }
    bool   startsWith(const char* s) const { return _s.rfind(s,0)==0; }
    bool   endsWith(const char* s)   const { size_t n=strlen(s); return _s.length()>=n && _s.compare(_s.length()-n,n,s)==0; }
    String toLowerCase() const { std::string r=_s; for(auto& c:r) c=tolower(c); return String(r); }
    String toUpperCase() const { std::string r=_s; for(auto& c:r) c=toupper(c); return String(r); }
    void   trim() { auto l=_s.find_first_not_of(" \t\n\r"); auto r=_s.find_last_not_of(" \t\n\r"); if(l==std::string::npos){_s="";return;} _s=_s.substr(l,r-l+1); }
    bool   concat(const char* s)    { if (!s) return false; _s += s; return true; }
    bool   concat(const String& s)  { _s += s._s; return true; }
    int    compareTo(const String& o) const { return _s.compare(o._s); }
    const std::string& str() const { return _s; }
};

inline bool operator==(const char* s, const String& str) { return str == s; }
inline bool operator!=(const char* s, const String& str) { return str != s; }
inline String operator+(const char* s, const String& str) { return String(std::string(s?s:"") + str.c_str()); }

// ── millis / delay ────────────────────────────────────────────────────────────
inline uint32_t millis() {
    using namespace std::chrono;
    static auto start = steady_clock::now();
    return (uint32_t)duration_cast<milliseconds>(steady_clock::now() - start).count();
}
inline uint64_t micros() {
    using namespace std::chrono;
    static auto start = steady_clock::now();
    return (uint64_t)duration_cast<microseconds>(steady_clock::now() - start).count();
}
inline void delay(uint32_t ms) {
    std::this_thread::sleep_for(std::chrono::milliseconds(ms));
}

// ── Serial stub ───────────────────────────────────────────────────────────────
struct _SerialProxy {
    void begin(long) {}
    void printf(const char* fmt, ...) { va_list a; va_start(a,fmt); vprintf(fmt,a); va_end(a); }
    void println(const char* s) { puts(s); }
    void println(const String& s) { puts(s.c_str()); }
    void println(int n) { printf("%d\n", n); }
    void print(const char* s) { fputs(s, stdout); }
    void print(const String& s) { fputs(s.c_str(), stdout); }
    void print(int n) { printf("%d", n); }
    void flush() {}
};
inline _SerialProxy Serial;

// ── GPIO / RGB LED stubs ──────────────────────────────────────────────────────
// Only the constants src/ actually uses are defined. Deliberately NOT
// defining INPUT/OUTPUT macros: `INPUT` is a winuser.h struct type and the
// WebServer stub pulls in Windows headers in the same translation units.
#ifndef HIGH
  #define HIGH 1
#endif
#ifndef LOW
  #define LOW 0
#endif
#ifndef INPUT_PULLUP
  #define INPUT_PULLUP 0x05
#endif
inline void pinMode(uint8_t, uint8_t) {}
inline int  digitalRead(uint8_t) { return HIGH; }   // active-low button: never pressed
inline void digitalWrite(uint8_t, uint8_t) {}
inline void rgbLedWrite(uint8_t, uint8_t, uint8_t, uint8_t) {}

// ── Misc Arduino globals ──────────────────────────────────────────────────────
#ifndef min
  #define min(a,b) ((a)<(b)?(a):(b))
#endif
#ifndef max
  #define max(a,b) ((a)>(b)?(a):(b))
#endif
#define constrain(x,lo,hi) ((x)<(lo)?(lo):(x)>(hi)?(hi):(x))
// map() omitted: conflicts with std::map
