#ifndef CFGSERVER_H
#define CFGSERVER_H

#include <Arduino.h>
#include <WebServer.h>
#include "configmanager.h"
#include "queueapi.h"

class ConfigWebServer {
public:
  ConfigWebServer(ConfigManager& cfgMgr, QueueApi& api);

  void begin();
  void handleClient();
  void stop();
  bool isConfigUpdated();
  void clearConfigFlag();

private:
  WebServer      _server;
  ConfigManager& _cfgMgr;
  QueueApi&      _api;
  bool           _configUpdated;
  bool           _pendingFactoryReset = false;  // restart deferred to handleClient()
  bool           _started             = false;  // makes begin()/stop() idempotent
  bool           _handlersRegistered  = false;  // register routes only once

  void handleRoot();
  void handleApiParks();
  void handleApiRides();
  void handleApiConfig();
  void handleSaveConfig();
  void handleFactoryReset();
  void handleNotFound();
  String buildConfigPage();
};

#endif // CFGSERVER_H
