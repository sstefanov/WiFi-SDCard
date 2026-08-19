#ifndef _SD_CONTROL_H_
#define _SD_CONTROL_H_

#define SPI_BLOCKOUT_PERIOD	1500UL

class SDControl {
public:
  SDControl() { }
  static void setup();
  static void takeBusControl();
  static void relinquishBusControl();
  static bool canWeTakeBus();
 
private:
  static void IRAM_ATTR onCsSenseFalling();
  static volatile unsigned long _spiBlockoutTime;
  static volatile bool _csSenseInterruptFired;
  static bool _weTookBus;
};

extern SDControl sdcontrol;

#endif
