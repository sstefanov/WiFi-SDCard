#include <ESP8266WiFi.h>
#include "sdControl.h"
#include "pins.h"

volatile unsigned long SDControl::_spiBlockoutTime = 0;
volatile bool SDControl::_csSenseInterruptFired = false;
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
	unsigned long now = millis();

	if(_csSenseInterruptFired) {
		_csSenseInterruptFired = false;
		// Confirm the pin is actually low before arming; the edge may have
		// already passed by the time we get here.
		if(!_weTookBus && digitalRead(CS_SENSE) == LOW) {
			_spiBlockoutTime = now + SPI_BLOCKOUT_PERIOD;
		}
	}

	if(now < _spiBlockoutTime) {
		return false;
	}

	// Timer just ended: if CS_SENSE is still asserted, restart it instead of
	// releasing the bus, giving the printer additional time.
	if(!_weTookBus && digitalRead(CS_SENSE) == LOW) {
		_spiBlockoutTime = now + SPI_BLOCKOUT_PERIOD;
		return false;
	}

	return true;
}
