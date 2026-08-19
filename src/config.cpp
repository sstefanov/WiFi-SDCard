#include <ESP8266WiFi.h>
#include <SPI.h>
#include <SdFat.h>
#include <EEPROM.h>
#include "pins.h"
#include "config.h"
#include "serial.h"
#include "sdControl.h"
#include "ESPWebDAV.h"

int Config::loadSD() {
  
  SERIAL_ECHOLN("Going to load config from INI file");

  if(!sdcontrol.canWeTakeBus()) {
    SERIAL_ECHOLN("Marlin is controling the bus");
    return -1;
  }
  sdcontrol.takeBusControl();
  SdFat sdfat;
  if(!sdfat.begin(SD_CS, SD_SPI_SPEED)) {
    SERIAL_ECHOLN("Initial SD failed");
    sdcontrol.relinquishBusControl();
    return -2;
  }

  SdFatFile file = sdfat.open("SETUP.INI", FILE_READ);
  if (!file) {
    SERIAL_ECHOLN("Open INI file failed");
    sdcontrol.relinquishBusControl();
    return -3;
  }

  // Get SSID and PASSWORD from file
  int rst = 0,step = 0;
  String buffer,sKEY,sValue;
  while (file.available()) { // check for EOF
    buffer = file.readStringUntil('\n');
    if(buffer.length() == 0) continue; // Empty line
    buffer.replace("\r", ""); // Delete all CR
    int iS = buffer.indexOf('='); // Get the seperator
    if(iS < 0) continue; // Bad line
    sKEY = buffer.substring(0,iS);
    sValue = buffer.substring(iS+1);
    if(sKEY == "SSID") {
      SERIAL_ECHOLN("INI file : SSID found");
      if(sValue.length()>0) {
        memset(data.ssid,'\0',WIFI_SSID_LEN);
        sValue.toCharArray(data.ssid,WIFI_SSID_LEN);
        step++;
      }
      else {
        rst = -4;
        goto FAIL;
      }
    }
    else if(sKEY == "PASSWORD") {
      SERIAL_ECHOLN("INI file : PASSWORD found");
      if(sValue.length()>0) {
        memset(data.psw,'\0',WIFI_PASSWD_LEN);
        sValue.toCharArray(data.psw,WIFI_PASSWD_LEN);
        step++;
      }
      else {
        rst = -5;
        goto FAIL;
      }
    }
    else continue; // Bad line
  }
  if(step != 2) { // We miss ssid or password
    //memset(data,) // TODO: do we need to empty the data?
    SERIAL_ECHOLN("Please check your SSDI or PASSWORD in ini file");
    rst = -6;
    goto FAIL;
  }

  FAIL:
  file.close();
  sdcontrol.relinquishBusControl();
  return rst;
}

unsigned char Config::load() {
  // Try to get the config from ini file
  if(0 == loadSD())
  {
    return 1; // Return as connected before
  }

  SERIAL_ECHOLN("Going to load config from EEPROM");

  EEPROM.begin(EEPROM_SIZE);
  uint8_t *p = (uint8_t*)(&data);
  for (size_t i = 0; i < sizeof(data); i++)
  {
    *(p + i) = EEPROM.read(i);
  }
  EEPROM.commit();

  if (data.flag != 1) {
    data.flag = 0;
    memset(data.ssid, '\0', sizeof(data.ssid));
    memset(data.psw, '\0', sizeof(data.psw));
    data.timezoneMinutes = 0;
    data.manualEpoch = 0;
  }

  if (data.timezoneMinutes < -720 || data.timezoneMinutes > 840) {
    data.timezoneMinutes = 0;
  }
  if (data.manualEpoch < 946684800UL || data.manualEpoch > 4102444800UL) {
    data.manualEpoch = 0;
  }

  if(data.flag) {
    SERIAL_ECHOLN("Going to use the old config to connect the network");
  }
  SERIAL_ECHOLN("We didn't connect the network before");
  return data.flag;
}

char* Config::ssid() {
  return data.ssid;
}

void Config::ssid(char* ssid) {
  if(ssid == NULL) return;
  strncpy(data.ssid,ssid,WIFI_SSID_LEN);
}

char* Config::password() {
  return data.psw;
}

void Config::password(char* password) {
  if(password == NULL) return;
  strncpy(data.psw,password,WIFI_PASSWD_LEN);
}

void Config::save(const char*ssid,const char*password) {
  if(ssid ==NULL || password==NULL)
    return;

  EEPROM.begin(EEPROM_SIZE);
  data.flag = 1;
  strncpy(data.ssid, ssid, WIFI_SSID_LEN);
  strncpy(data.psw, password, WIFI_PASSWD_LEN);
  uint8_t *p = (uint8_t*)(&data);
  for (size_t i = 0; i < sizeof(data); i++)
  {
    EEPROM.write(i, *(p + i));
  }
  EEPROM.commit();
}

void Config::save() {
  if(data.ssid == NULL || data.psw == NULL)
    return;

  EEPROM.begin(EEPROM_SIZE);
  data.flag = 1;
  uint8_t *p = (uint8_t*)(&data);
  for (size_t i = 0; i < sizeof(data); i++)
  {
    EEPROM.write(i, *(p + i));
  }
  EEPROM.commit();
}

int16_t Config::timezoneMinutes() const {
  return data.timezoneMinutes;
}

void Config::timezoneMinutes(int16_t minutes) {
  if (minutes < -720) minutes = -720;
  if (minutes > 840) minutes = 840;
  data.timezoneMinutes = minutes;
}

uint32_t Config::manualEpoch() const {
  return data.manualEpoch;
}

void Config::manualEpoch(uint32_t epoch) {
  if (epoch < 946684800UL || epoch > 4102444800UL) {
    data.manualEpoch = 0;
  } else {
    data.manualEpoch = epoch;
  }
}

void Config::printAllData(const char *title) {
    SERIAL_ECHOLN("");
    SERIAL_ECHO("=== ");
    SERIAL_ECHO(title);
    SERIAL_ECHOLN(" ===");

    // Print SSID and Password as strings
    SERIAL_ECHO("SSID: ");
    SERIAL_ECHOLN(data.ssid);
    SERIAL_ECHO("Password: ");
    SERIAL_ECHOLN(data.psw);

    // Print the flag
    SERIAL_ECHO("Flag: ");
    SERIAL_ECHOLN(data.flag);
    SERIAL_ECHO("Timezone minutes: ");
    SERIAL_ECHOLN(data.timezoneMinutes);
    SERIAL_ECHO("Manual epoch: ");
    SERIAL_ECHOLN((unsigned long)data.manualEpoch);

    // Print raw bytes
    SERIAL_ECHOLN("Raw bytes:");
    uint8_t *p = (uint8_t *)(&data);
    for (size_t i = 0; i < sizeof(data); i++) {
        if (i % 16 == 0)
            SERIAL_ECHOLN(""); // new line every 16 bytes
        char buf[4];
        sprintf(buf, "%02X ", p[i]);
        SERIAL_ECHO(buf);
    }
    SERIAL_ECHOLN("");
}

Config config;
