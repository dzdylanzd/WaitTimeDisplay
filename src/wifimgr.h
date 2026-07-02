#ifndef WIFIMGR_H
#define WIFIMGR_H

#include <Arduino.h>
#include <Preferences.h>
#include <DNSServer.h>
#include <WebServer.h>

class WiFiManager {
public:
  WiFiManager();

  void loadCredentials();
  void saveCredentials(const String& ssid, const String& pass);
  void clearCredentials();

  bool isConfigured() const { return _configured; }
  const String& getSSID()     const { return _ssid; }
  const String& getPassword() const { return _pass; }

  void runCaptivePortal();
  bool connect();
  void resetConnecting();

private:
  Preferences _prefs;
  String _ssid;
  String _pass;
  bool   _configured;
  bool   _connecting  = false;
  bool   _portalSaved = false;  // new credentials saved during this portal session

  DNSServer  _dnsServer;
  WebServer  _webServer;

  void startAP();
  void startDNSServer();
  void startHTTPServer();
  void stopServers();
};

#endif // WIFIMGR_H
