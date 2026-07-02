// Real HTTPS client using Windows WinHTTP.
// WinHTTP (unlike WinINet) is safe to call from background threads,
// which is required since QueueApi is called from httplib handler threads.
#pragma once
#include "Arduino.h"
#include "WiFiClientSecure.h"
#include <windows.h>
#include <winhttp.h>
#include <string>

static inline std::wstring _httpToWide(const std::string& s) {
    if (s.empty()) return {};
    int n = MultiByteToWideChar(CP_UTF8, 0, s.c_str(), (int)s.size(), nullptr, 0);
    std::wstring w(n, 0);
    MultiByteToWideChar(CP_UTF8, 0, s.c_str(), (int)s.size(), &w[0], n);
    return w;
}

class HTTPClient {
    HINTERNET _hSession = nullptr;
    HINTERNET _hConnect = nullptr;
    HINTERNET _hRequest = nullptr;
    std::string _body;
    int _statusCode = -1;
    int _timeout    = 10000;

public:
    HTTPClient() {
        _hSession = WinHttpOpen(L"QueueWatch-Sim/1.0",
            WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,
            WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
    }
    ~HTTPClient() {
        end();
        if (_hSession) { WinHttpCloseHandle(_hSession); _hSession = nullptr; }
    }

    bool begin(WiFiClientSecure&, const String& url) { return begin(url); }

    bool begin(const String& url) {
        end();
        if (!_hSession) return false;

        std::wstring wurl = _httpToWide(std::string(url.c_str()));

        URL_COMPONENTS uc = {};
        uc.dwStructSize = sizeof(uc);
        wchar_t host[256] = {}, path[4096] = {};
        uc.lpszHostName    = host; uc.dwHostNameLength    = _countof(host);
        uc.lpszUrlPath     = path; uc.dwUrlPathLength     = _countof(path);

        if (!WinHttpCrackUrl(wurl.c_str(), 0, 0, &uc)) {
            fprintf(stderr, "[HTTP] WinHttpCrackUrl failed for: %s\n", url.c_str());
            return false;
        }

        _hConnect = WinHttpConnect(_hSession, host, uc.nPort, 0);
        if (!_hConnect) {
            fprintf(stderr, "[HTTP] WinHttpConnect failed (err %lu)\n", GetLastError());
            return false;
        }

        DWORD reqFlags = (uc.nScheme == INTERNET_SCHEME_HTTPS) ? WINHTTP_FLAG_SECURE : 0;
        _hRequest = WinHttpOpenRequest(_hConnect, L"GET", path,
            nullptr, WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES, reqFlags);
        if (!_hRequest) {
            fprintf(stderr, "[HTTP] WinHttpOpenRequest failed (err %lu)\n", GetLastError());
            return false;
        }

        // Allow imperfect certs (queue-times.com is legit, but just in case)
        DWORD secFlags = SECURITY_FLAG_IGNORE_UNKNOWN_CA |
                         SECURITY_FLAG_IGNORE_CERT_WRONG_USAGE |
                         SECURITY_FLAG_IGNORE_CERT_CN_INVALID  |
                         SECURITY_FLAG_IGNORE_CERT_DATE_INVALID;
        WinHttpSetOption(_hRequest, WINHTTP_OPTION_SECURITY_FLAGS, &secFlags, sizeof(secFlags));

        return true;
    }

    void setTimeout(int ms) {
        _timeout = ms;
        if (_hRequest) WinHttpSetTimeouts(_hRequest, ms, ms, ms, ms);
    }

    int GET() {
        if (!_hRequest) return -1;
        _body.clear(); _statusCode = -1;

        if (!WinHttpSendRequest(_hRequest, WINHTTP_NO_ADDITIONAL_HEADERS, 0,
                                WINHTTP_NO_REQUEST_DATA, 0, 0, 0)) {
            fprintf(stderr, "[HTTP] WinHttpSendRequest failed (err %lu)\n", GetLastError());
            return -1;
        }
        if (!WinHttpReceiveResponse(_hRequest, nullptr)) {
            fprintf(stderr, "[HTTP] WinHttpReceiveResponse failed (err %lu)\n", GetLastError());
            return -1;
        }

        DWORD code = 0, sz = sizeof(DWORD);
        WinHttpQueryHeaders(_hRequest,
            WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
            WINHTTP_HEADER_NAME_BY_INDEX, &code, &sz, WINHTTP_NO_HEADER_INDEX);
        _statusCode = (int)code;

        DWORD avail = 0, read = 0;
        while (WinHttpQueryDataAvailable(_hRequest, &avail) && avail > 0) {
            std::string chunk(avail, '\0');
            if (!WinHttpReadData(_hRequest, &chunk[0], avail, &read)) break;
            chunk.resize(read);
            _body += chunk;
        }

        return _statusCode;
    }

    int    getSize() const { return (int)_body.size(); }
    String getString()    { return String(_body.c_str()); }

    void end() {
        if (_hRequest) { WinHttpCloseHandle(_hRequest); _hRequest = nullptr; }
        if (_hConnect) { WinHttpCloseHandle(_hConnect); _hConnect = nullptr; }
    }
};
