#ifndef _CONFIG_H_
#define _CONFIG_H_

#include <stdio.h>
#include <string.h>

#define WIFI_SSID_LEN 32
#define WIFI_PASSWD_LEN 64

#define EEPROM_SIZE 512

typedef struct config_type
{
  unsigned char flag; // Was saved before?
  char ssid[32];
  char psw[64];
  int16_t timezoneMinutes;
  uint32_t manualEpoch;
}CONFIG_TYPE;

class Config	{
public:
	unsigned char load();
  char* ssid();
  void ssid(char* ssid);
  char* password();
  void password(char* password);
  void save(const char*ssid,const char*password);
  void save();
  // save_ip() function removed - was causing SD card corruption and not needed
  int16_t timezoneMinutes() const;
  void timezoneMinutes(int16_t minutes);
  uint32_t manualEpoch() const;
  void manualEpoch(uint32_t epoch);
  void printAllData(const char *title);
protected:
  CONFIG_TYPE data;
};

extern Config config;

#endif
