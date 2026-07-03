#ifndef CFGSERVER_H
#define CFGSERVER_H

#include <Arduino.h>
#include <WebServer.h>
#include "configmanager.h"
#include "queueapi.h"
#include "otaupdater.h"

// Web-facing OTA status, decoupled from AppStateManager's SystemState so
// this header doesn't need to include appstate.h (which already includes
// this one). AppStateManager maps its internal OTA states onto this when
// calling setOtaStatus().
enum class OtaUiState { Idle, Checking, Downloading, Installing, Success, Error };

class ConfigWebServer {
public:
  ConfigWebServer(ConfigManager& cfgMgr, QueueApi& api);

  void begin();
  void handleClient();
  void stop();
  bool isConfigUpdated();
  void clearConfigFlag();

  // OTA: the web UI's "Install" button sets a pending flag (plus the asset
  // URL from the preceding "Check" click); AppStateManager::tickWaitTimeCycle()
  // polls and consumes it (mirroring isConfigUpdated()'s poll-and-clear
  // idiom) to drive the actual download/flash through its own state
  // machine. Status flows back out via setOtaStatus() so /api/ota/status
  // can report progress to the page.
  bool consumeOtaStartRequest(String& outAssetUrl);
  void setOtaStatus(OtaUiState state, uint8_t progressPct, const String& message = "");

private:
  WebServer      _server;
  ConfigManager& _cfgMgr;
  QueueApi&      _api;
  OtaUpdater     _ota;   // used for the synchronous "check" only; the actual
                         // download/flash runs through AppStateManager's own
                         // OtaUpdater instance, driven by the state machine
  bool           _configUpdated;
  bool           _pendingFactoryReset = false;  // restart deferred to handleClient()
  bool           _started             = false;  // makes begin()/stop() idempotent
  bool           _handlersRegistered  = false;  // register routes only once

  bool           _pendingOtaStart     = false;
  String         _otaLatestVersion;              // populated by handleApiOtaCheck()
  String         _otaLatestAssetUrl;
  OtaUiState     _otaState            = OtaUiState::Idle;
  uint8_t        _otaProgressPct      = 0;
  String         _otaMessage;

  void handleRoot();
  void handleApiParks();
  void handleApiRides();
  void handleApiConfig();
  void handleSaveConfig();
  void handleFactoryReset();
  void handleApiOtaCheck();
  void handleApiOtaStart();
  void handleApiOtaStatus();
  void handleNotFound();
};

#endif // CFGSERVER_H
