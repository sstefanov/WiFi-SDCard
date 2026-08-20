#ifndef _NETWORK_H_
#define _NETWORK_H_

#include <stdint.h>

#define HOSTNAME		"FYSETC"
#define SERVER_PORT		80

#define WIFI_CONNECT_TIMEOUT 30000UL

class Network {
public:
  Network() { initFailed = true; wifiConnecting = true; _lastDavRetryTime = 0; _ntpSynced = false; _timezoneMinutes = 0; }
  bool start();
  
  void handleHttp();   // Always active
  void handleWebDAV(); // Only active if SD mounted
  bool isConnected();
  bool isConnecting();
  void handle();
  bool ready();
  bool isNtpSynced() const;
  int16_t timezoneMinutes() const;
  void setTimezoneMinutes(int16_t minutes);
  uint32_t currentEpoch() const;
  bool applyManualEpoch(uint32_t epoch);
  void clearManualEpoch();
  uint32_t manualEpoch() const;
  int SDmounted;

private:
  bool wifiConnected;
  bool wifiConnecting;
  bool initFailed;
  bool _ntpSynced;
  int16_t _timezoneMinutes;
  unsigned long _lastDavRetryTime;
  int startDAVServer();
};

extern Network network;

#endif
