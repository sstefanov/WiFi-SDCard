// Using the WebDAV server with Rigidbot 3D printer.
// Printer controller is a variation of Rambo running Marlin firmware

#include "config.h"
#include "gcode.h"
#include "network.h"
#include "parser.h"
#include "sdControl.h"
#include "serial.h"
#include <ArduinoOTA.h>

// LED is connected to GPIO2 on this board
#define INIT_LED			{pinMode(2, OUTPUT);}
#define LED_ON				{digitalWrite(2, LOW);}
#define LED_OFF				{digitalWrite(2, HIGH);}

bool sdMounted = false;
// ------------------------
void setup() {
	SERIAL_INIT(115200);
	INIT_LED;
	blink();
	
	sdcontrol.setup();

	// ----- WIFI -------
    unsigned char cfg = config.load();
    if (cfg == 1) { // Connected before
        if (!network.start()) {
            SERIAL_ECHOLN("Connect fail, please check your INI file or set the wifi config and connect again");
            SERIAL_ECHOLN("- M50: Set the wifi ssid , 'M50 ssid-name'");
            SERIAL_ECHOLN("- M51: Set the wifi password , 'M51 password'");
            SERIAL_ECHOLN("- M52: Start to connect the wifi");
            SERIAL_ECHOLN("- M53: Check the connection status");
        }
  }
  else {
    SERIAL_ECHOLN("Welcome to FYSETC: www.fysetc.com");
    SERIAL_ECHOLN("Please set the wifi config first");
    SERIAL_ECHOLN("- M50: Set the wifi ssid , 'M50 ssid-name'");
    SERIAL_ECHOLN("- M51: Set the wifi password , 'M51 password'");
    SERIAL_ECHOLN("- M52: Start to connect the wifi");
    SERIAL_ECHOLN("- M53: Check the connection status");
  }
  // OTA setup (only if WiFi is ready)
  if (WiFi.status() == WL_CONNECTED) {
      SERIAL_ECHOLN("OTA setup");
      ArduinoOTA.setHostname("ESPWebDAV"); // Optional: customize name
      ArduinoOTA.setPassword("1234");      // Optional: add OTA password

      ArduinoOTA.onStart([]() {
          SERIAL_ECHOLN("OTA Update Start");
      });
      ArduinoOTA.onEnd([]() {
          SERIAL_ECHOLN("\nOTA Update End");
      });
      ArduinoOTA.onProgress([](unsigned int progress, unsigned int total) {
          SERIAL_ECHO("OTA Progress: ");
          SERIAL_ECHOLN((progress * 100) / total);
      });
      ArduinoOTA.onError([](ota_error_t error) {
          SERIAL_ECHO("OTA Error: ");
          SERIAL_ECHOLN(error);
      });
      SERIAL_ECHOLN("OTA Ready at IP: " + WiFi.localIP().toString());
      ArduinoOTA.begin();
      delay(500);
      SERIAL_ECHOLN("OTA Ready");
  }
}

  // ------------------------
void loop() {
      // handle the request
      network.handleHttp();
      network.handleWebDAV();

      // Handle gcode
      gcode.Handle();
      
      ArduinoOTA.handle(); // Keep OTA responsive
      // blink
      statusBlink();
}

// ------------------------
void blink()	{
// ------------------------
	LED_ON; 
	delay(100); 
	LED_OFF; 
	delay(400);
}

// ------------------------
void errorBlink()	{
// ------------------------
	for(int i = 0; i < 100; i++)	{
		LED_ON; 
		delay(50); 
		LED_OFF; 
		delay(50);
	}
}

void statusBlink() {
  static unsigned long time = 0;
  if(millis() > time + 1000 ) {
    if(network.isConnecting()) {
      LED_OFF;
    }
    else if(network.isConnected()) {
      LED_ON; 
  		delay(50); 
  		LED_OFF; 
    }
    else {
      LED_ON;
    }
    time = millis();
  }

  // SPI bus not ready
	//if(millis() < spiBlockoutTime)
	//	blink();
}
