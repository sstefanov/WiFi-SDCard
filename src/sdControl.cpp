#include <ESP8266WiFi.h>
#include "sdControl.h"
#include "pins.h"

volatile unsigned long SDControl::_spiBlockoutTime = 0;
volatile bool SDControl::_csSenseInterruptFired = false;
volatile bool SDControl::_cardAvailable = false;
bool SDControl::_weTookBus = false;

void IRAM_ATTR SDControl::onCsSenseFalling() {
	_csSenseInterruptFired = true;
}

void SDControl::setup() {
  // ----- GPIO -------
	// Detect when other master uses SPI bus
	pinMode(CS_SENSE, INPUT_PULLUP);
	attachInterrupt(CS_SENSE, SDControl::onCsSenseFalling, FALLING);

	// Short startup settle delay to avoid immediate false edges.
	delay(100);
}

// ------------------------
void SDControl::takeBusControl()	{
// ------------------------
	_weTookBus = true;
	//LED_ON;
	pinMode(MISO_PIN, SPECIAL);	
	pinMode(MOSI_PIN, SPECIAL);	
	pinMode(SCLK_PIN, SPECIAL);	
	pinMode(SD_CS, OUTPUT);
}

// ------------------------
void SDControl::relinquishBusControl()	{
// ------------------------
	pinMode(MISO_PIN, INPUT);	
	pinMode(MOSI_PIN, INPUT);	
	pinMode(SCLK_PIN, INPUT);	
	pinMode(SD_CS, INPUT);
	//LED_OFF;
	_weTookBus = false;
}

bool SDControl::canWeTakeBus() {
	_csSenseInterruptFired = false;

	if (_weTookBus) {
		return true;
	}

	unsigned long now = millis();
	if (digitalRead(CS_SENSE) == LOW) {
		// Every printer access starts the quiet-period timer over.
		_spiBlockoutTime = 0;
		_cardAvailable = false;
		return false;
	}

	if (!_cardAvailable) {
		if (_spiBlockoutTime == 0) {
			_spiBlockoutTime = now + SPI_BLOCKOUT_PERIOD;
			return false;
		}
		if ((long)(now - _spiBlockoutTime) < 0) {
			return false;
		}
		_cardAvailable = true;
	}

	return true;
}
