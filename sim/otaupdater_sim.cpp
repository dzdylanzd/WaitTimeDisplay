// Simulator stub: no real flash to write to, so OTA always reports
// "no update"/"not supported". Lets the sim safely exercise the state-
// machine and web-UI wiring without ever touching real hardware.
#include "../src/otaupdater.h"

bool OtaUpdater::checkForUpdate(String&, String&) {
    return false;
}

OtaResult OtaUpdater::performUpdate(const String&, OtaProgressFn) {
    printf("[sim] OTA not supported in simulator\n");
    return OtaResult::ErrorNotSupported;
}

String OtaUpdater::resolveRedirect(const String&) {
    return "";
}

bool OtaUpdater::isPendingConfirmation() {
    return false;
}

void OtaUpdater::markBootSuccessful() {}
