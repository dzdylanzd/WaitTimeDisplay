// WebServer stub using cpp-httplib (HTTP only — no TLS on localhost).
// Runs in a background thread; handleClient() is a no-op.
// Port is forced to 8080 in the sim regardless of the constructor argument
// (port 80 requires administrator on Windows).
#pragma once

#define CPPHTTPLIB_THREAD_POOL_COUNT 2
#include "httplib.h"
#include "Arduino.h"
#include <functional>
#include <mutex>
#include <thread>

// Arduino WebServer HTTP method enum
enum HTTPMethod { HTTP_ANY = 0, HTTP_GET, HTTP_POST, HTTP_PUT, HTTP_DELETE, HTTP_PATCH };

class WebServer {
    httplib::Server  _svr;
    std::thread      _thr;
    int              _port;
    std::mutex       _reqMtx;

    const httplib::Request* _curReq = nullptr;
    httplib::Response*      _curRes = nullptr;

    void _wrap(const char* path, std::function<void()> handler) {
        auto fn = [this, handler](const httplib::Request& req, httplib::Response& res) {
            std::lock_guard<std::mutex> lock(_reqMtx);
            _curReq = &req;
            _curRes = &res;
            handler();
            _curReq = nullptr;
            _curRes = nullptr;
        };
        _svr.Get(path, fn);
        _svr.Post(path, fn);
    }

public:
    // Force port 8080 in the sim — port 80 requires elevation on Windows
    explicit WebServer(int /*port*/ = 80) : _port(8080) {}
    ~WebServer() { stop(); }

    void on(const char* path, std::function<void()> h) { _wrap(path, h); }

    void on(const char* path, HTTPMethod method, std::function<void()> h) {
        auto fn = [this, h](const httplib::Request& req, httplib::Response& res) {
            std::lock_guard<std::mutex> lock(_reqMtx);
            _curReq = &req; _curRes = &res;
            h();
            _curReq = nullptr; _curRes = nullptr;
        };
        switch (method) {
        case HTTP_GET:  _svr.Get(path, fn);  break;
        case HTTP_POST: _svr.Post(path, fn); break;
        default:        _svr.Get(path, fn); _svr.Post(path, fn); break;
        }
    }

    // NOTE: deliberately NOT httplib's set_error_handler() -- that fires for
    // ANY response with status >= 400, including ones a matched route's own
    // handler intentionally sent (e.g. a 400 validation error), silently
    // overwriting it. Arduino's real WebServer::onNotFound() only fires when
    // no route matches at all, so a catch-all route (registered last, after
    // every specific .on() call in ConfigWebServer::begin()) is the correct
    // equivalent: httplib tries registered patterns in registration order,
    // so this is only reached when nothing more specific matched first.
    void onNotFound(std::function<void()> h) {
        auto fn = [this, h](const httplib::Request& req, httplib::Response& res) {
            std::lock_guard<std::mutex> lock(_reqMtx);
            _curReq = &req; _curRes = &res;
            h();
            _curReq = nullptr; _curRes = nullptr;
        };
        _svr.Get(".*", fn);
        _svr.Post(".*", fn);
    }

    void begin() {
        if (!_svr.bind_to_port("0.0.0.0", _port)) {
            fprintf(stderr, "[WebServer] Failed to bind port %d\n", _port);
            return;
        }
        _thr = std::thread([this]() { _svr.listen_after_bind(); });
        printf("[WebServer] Config UI: http://localhost:%d\n", _port);
    }

    void handleClient() {}   // no-op

    void stop()  { _svr.stop(); if (_thr.joinable()) _thr.join(); }
    void close() { stop(); }  // Arduino alias

    void send(int code, const char* contentType, const String& body) {
        if (_curRes) {
            _curRes->status = code;
            _curRes->set_content(body.c_str(), contentType);
        }
    }

    // PROGMEM is a no-op in the sim (see sim/Arduino.h), so content is just a
    // normal null-terminated buffer here — matches the real ESP32 WebServer's
    // send_P(code, contentType, PGM_P content, size_t contentLength = 0).
    void send_P(int code, const char* contentType, const char* content,
                size_t contentLength = 0) {
        if (_curRes) {
            size_t len = contentLength ? contentLength : strlen(content);
            _curRes->status = code;
            _curRes->set_content(content, len, contentType);
        }
    }

    String arg(const char* name) {
        if (!_curReq) return "";
        if (strcmp(name, "plain") == 0) return String(_curReq->body.c_str());
        if (_curReq->has_param(name)) return String(_curReq->get_param_value(name).c_str());
        return "";
    }

    bool hasArg(const char* name) {
        if (!_curReq) return false;
        if (strcmp(name, "plain") == 0) return !_curReq->body.empty();
        return _curReq->has_param(name);
    }

    String uri()    { return _curReq ? String(_curReq->path.c_str()) : ""; }
    String method() { return _curReq ? String(_curReq->method.c_str()) : ""; }
};
