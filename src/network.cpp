#include "network.h"
#include "serial.h"
#include "config.h"
#include "pins.h"
#include "ESP8266WiFi.h"
#include "ESPWebDAV.h"
#include "sdControl.h"
#include <time.h>
#include <SdFat.h>

namespace {
constexpr time_t NTP_MIN_VALID_EPOCH = 1577836800; // 2020-01-01 UTC
int16_t gTimezoneMinutes = 0;

void sdDateTimeCallback(uint16_t* date, uint16_t* time) {
  time_t now = ::time(nullptr);
  struct tm t;

  now += (time_t)gTimezoneMinutes * 60;

  if (now < NTP_MIN_VALID_EPOCH || !gmtime_r(&now, &t)) {
    *date = FAT_DATE(2024, 1, 1);
    *time = FAT_TIME(0, 0, 0);
    return;
  }

  uint16_t year = (uint16_t)(t.tm_year + 1900);
  uint8_t month = (uint8_t)(t.tm_mon + 1);
  uint8_t day = (uint8_t)t.tm_mday;
  uint8_t hour = (uint8_t)t.tm_hour;
  uint8_t minute = (uint8_t)t.tm_min;
  uint8_t second = (uint8_t)t.tm_sec;

  if (year < 1980) year = 1980;
  *date = FAT_DATE(year, month, day);
  *time = FAT_TIME(hour, minute, second);
}

bool syncTimeFromNtp() {
  configTime(0, 0, "pool.ntp.org", "time.nist.gov", "time.google.com");

  const uint8_t maxChecks = 40; // ~8 seconds
  for (uint8_t i = 0; i < maxChecks; i++) {
    time_t now = ::time(nullptr);
    if (now >= NTP_MIN_VALID_EPOCH) {
      SERIAL_ECHO("NTP time synced: ");
      SERIAL_ECHOLN((unsigned long)now);
      return true;
    }
    delay(200);
  }

  SERIAL_ECHOLN("NTP sync timeout, continuing with fallback timestamp");
  return false;
}
} // namespace

String IpAddress2String(const IPAddress& ipAddress)
{
  return String(ipAddress[0]) + String(".") +\
  String(ipAddress[1]) + String(".") +\
  String(ipAddress[2]) + String(".") +\
  String(ipAddress[3])  ;
}

bool Network::start() {
  wifiConnected = false;
  wifiConnecting = true;
  
  // Set hostname first
  WiFi.hostname(HOSTNAME);
  // Reduce startup surge current
  WiFi.setAutoConnect(false);
  WiFi.mode(WIFI_STA);
  WiFi.setPhyMode(WIFI_PHY_MODE_11N);
  WiFi.begin(config.ssid(), config.password());

  // Wait for connection
  unsigned int timeout = 0;
  while(WiFi.status() != WL_CONNECTED) {
    //blink();
    SERIAL_ECHO(".");
    timeout++;
    if(timeout > WIFI_CONNECT_TIMEOUT/100) {
      SERIAL_ECHOLN("");
      wifiConnecting = false;
      return false;
    }
    else
      delay(100);
  }

  SERIAL_ECHOLN("");
  SERIAL_ECHO("Connected to "); SERIAL_ECHOLN(config.ssid());
  SERIAL_ECHO("IP address: "); SERIAL_ECHOLN(WiFi.localIP());
  SERIAL_ECHO("RSSI: "); SERIAL_ECHOLN(WiFi.RSSI());
  SERIAL_ECHO("Mode: "); SERIAL_ECHOLN(WiFi.getPhyMode());
  SERIAL_ECHO("Asscess to SD at the Run prompt : \\\\"); SERIAL_ECHO(WiFi.localIP());SERIAL_ECHOLN("\\DavWWWRoot");

  _timezoneMinutes = config.timezoneMinutes();
  gTimezoneMinutes = _timezoneMinutes;

  uint32_t savedManualEpoch = config.manualEpoch();
  if (savedManualEpoch > 0) {
    applyManualEpoch(savedManualEpoch);
  }

  _ntpSynced = syncTimeFromNtp();
  FatFile::dateTimeCallback(sdDateTimeCallback);

  SERIAL_ECHOLN("Going to start DAV server");
  startDAVServer();

  wifiConnected = true;
  config.save();
  String sIp = IpAddress2String(WiFi.localIP());
  // IP address is displayed in serial console, no need to save to SD card
  SERIAL_ECHO("IP Address: ");
  SERIAL_ECHOLN(sIp);
  wifiConnecting = false;

  return true;
}

int Network::startDAVServer() {
  SERIAL_ECHOLN("[DAV] startDAVServer: begin");
  if(!dav.ensureServer(SERVER_PORT)) {
    SERIAL_ECHOLN("[DAV] startDAVServer: server init failed");
    return -1;
  }

  SERIAL_ECHOLN("FYSETC WebDAV server started; SD access is on demand");
  return 0;
}

bool Network::isConnected() {
  return wifiConnected;
}

bool Network::isConnecting() {
  return wifiConnecting;
}

// a client is waiting and FS is ready and other SPI master is not using the bus
bool Network::ready() {
    if (!isConnected())
        return false;

    // do it only if there is a need to read FS
	if(!dav.isClientWaiting())	return false;
    // has other master been using the bus in last few seconds
  if (!sdcontrol.canWeTakeBus()) {
      DBG_PRINTLN("Marlin is reading from SD card");
      // dav.rejectClient("Marlin is reading from SD card");
      return false;
  }
  DBG_PRINTLN("Network::ready");
    if(initFailed) {
      SERIAL_ECHOLN("[DAV] ready: retry init after previous failure");
        // dav.rejectClient("Failed to initialize SD Card");
        // try again:
        if (!sdcontrol.canWeTakeBus()) {
          SERIAL_ECHOLN("[DAV] ready: printer is using SD card");
          return false;
        }
        sdcontrol.takeBusControl();
        bool initialized = dav.init(SD_CS, SD_SPI_SPEED, SERVER_PORT);
        sdcontrol.relinquishBusControl();
        if (!initialized) {
        SERIAL_ECHOLN("[DAV] ready: retry failed");
            // indicate error on LED
            // errorBlink();
            initFailed = true;
            return false;
        } else {
            initFailed = false;
        SERIAL_ECHOLN("[DAV] ready: retry succeeded, SD mounted");
            return true;
        }
    }
    return true;
}

// ---------------- HTTP handler ----------------
void Network::handleHttp() {
    if (!wifiConnected)
        return;

    if (!dav.isServerReady()) {
      return;
    }

    dav.handleHttpClient(); // always active
}

void Network::handle() {
    DBG_PRINTLN("Network::handle");
    if(network.ready()) {
      sdcontrol.takeBusControl();
      dav.handleClient();
      sdcontrol.relinquishBusControl();
	  }
}
// ---------------- WebDAV handler ----------------
void Network::handleWebDAV() {
    if (!wifiConnected)
        return;

    if (!sdcontrol.canWeTakeBus()) {
        return; // printer holds the bus
    }

    if (dav.isClientWaiting()) {
      if (!sdcontrol.canWeTakeBus()) {
        dav.rejectClient("The printer is using SD card.");
        return;
      }
        sdcontrol.takeBusControl();
        dav.handleWebDAVClient();
        sdcontrol.relinquishBusControl();
    }
}

Network network;

bool Network::isNtpSynced() const {
  return _ntpSynced;
}

int16_t Network::timezoneMinutes() const {
  return _timezoneMinutes;
}

void Network::setTimezoneMinutes(int16_t minutes) {
  if (minutes < -720) minutes = -720;
  if (minutes > 840) minutes = 840;
  _timezoneMinutes = minutes;
  gTimezoneMinutes = minutes;
  config.timezoneMinutes(minutes);
}

uint32_t Network::currentEpoch() const {
  time_t now = ::time(nullptr);
  if (now < 0) return 0;
  return (uint32_t)now;
}

bool Network::applyManualEpoch(uint32_t epoch) {
  if (epoch < 946684800UL || epoch > 4102444800UL) {
    return false;
  }
  struct timeval tv;
  tv.tv_sec = (time_t)epoch;
  tv.tv_usec = 0;
  if (settimeofday(&tv, nullptr) != 0) {
    return false;
  }
  config.manualEpoch(epoch);
  return true;
}

void Network::clearManualEpoch() {
  config.manualEpoch(0);
}

uint32_t Network::manualEpoch() const {
  return config.manualEpoch();
}
