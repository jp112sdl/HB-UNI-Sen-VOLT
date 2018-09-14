//- -----------------------------------------------------------------------------------------------------------------------
// AskSin++
// 2016-10-31 papa Creative Commons - http://creativecommons.org/licenses/by-nc-sa/3.0/de/
// 2018-09-14 jp112sdl Creative Commons - http://creativecommons.org/licenses/by-nc-sa/3.0/de/
//- -----------------------------------------------------------------------------------------------------------------------

// define this to read the device id, serial and device type from bootloader section
// #define USE_OTA_BOOTLOADER

#define EI_NOTEXTERNAL
#include <EnableInterrupt.h>
#include <AskSinPP.h>
#include <LowPower.h>

#include <Register.h>
#include <MultiChannelDevice.h>

// Arduino Pro mini 8 Mhz
// Arduino pin for the config button
#define CONFIG_BUTTON_PIN  8
#define LED_PIN            4

// Number of voltage measurement channels
#define NUM_CHANNELS       2

// number of available peers per channel
#define PEERS_PER_CHANNEL  6

// all library classes are placed in the namespace 'as'
using namespace as;

//Korrekturfaktor der Clock-Ungenauigkeit, wenn keine RTC verwendet wird
#define SYSCLOCK_FACTOR    0.88

// define all device properties
const struct DeviceInfo PROGMEM devinfo = {
  {0xF3, 0x4A, 0x00},          // Device ID
  "JPVOLT0000",                // Device Serial
  {0xF3, 0x4A},                // Device Model
  0x10,                        // Firmware Version
  0x53,                        // Device Type
  {0x01, 0x01}                 // Info Bytes
};

/**
   Configure the used hardware
*/
typedef AskSin<StatusLed<LED_PIN>, BatterySensor, Radio<AvrSPI<10, 11, 12, 13>, 2>> Hal;
Hal hal;


DEFREGISTER(UReg0, MASTERID_REGS, DREG_LOWBATLIMIT, 0x20, 0x21)
class UList0 : public RegList0<UReg0> {
  public:
    UList0 (uint16_t addr) : RegList0<UReg0>(addr) {}

    bool Sendeintervall (uint16_t value) const {
      return this->writeRegister(0x20, (value >> 8) & 0xff) && this->writeRegister(0x21, value & 0xff);
    }
    uint16_t Sendeintervall () const {
      return (this->readRegister(0x20, 0) << 8) + this->readRegister(0x21, 0);
    }

    void defaults () {
      clear();
      lowBatLimit(22);
      Sendeintervall(180);
    }
};

DEFREGISTER(UReg1, 0x01, 0x02, 0x03)
class UList1 : public RegList1<UReg1> {
  public:
    UList1 (uint16_t addr) : RegList1<UReg1>(addr) {}
    void defaults () {
      clear();
    }
};

class MeasureEventMsg : public Message {
  public:
    void init(uint8_t msgcnt, uint8_t channel, uint16_t voltage, uint8_t battvolt) {
      Message::init(0x0e, msgcnt, 0x53, (msgcnt % 20 == 1) ? BIDI : BCAST, channel & 0xff, (voltage >> 8) & 0xff);
      pload[0] = voltage & 0xff;
      pload[1] = battvolt & 0xff;
    }
};

class MeasureChannel : public Channel<Hal, UList1, EmptyList, List4, PEERS_PER_CHANNEL, UList0>, public Alarm {
    MeasureEventMsg msg;
    uint16_t        voltage;
    uint8_t         last_flags = 0xff;

  public:
    MeasureChannel () : Channel(), Alarm(0), voltage(0) {}
    virtual ~MeasureChannel () {}

    void measure() {

      voltage = random(2300);


    }

    virtual void trigger (__attribute__ ((unused)) AlarmClock& clock) {
      measure();
      tick = delay();
      msg.init(device().nextcount(), number(), voltage,  device().battery().current());
      device().sendPeerEvent(msg, *this);
      sysclock.add(*this);
    }

    uint32_t delay () {
      uint16_t d = (max(10, device().getList0().Sendeintervall()) * SYSCLOCK_FACTOR) + random(10); //add some small random difference between channels
      return seconds2ticks(d);
    }

    void configChanged() {
    }

    void setup(Device<Hal, UList0>* dev, uint8_t number, uint16_t addr) {
      Channel::setup(dev, number, addr);
      sysclock.add(*this);
    }

    uint8_t status () const {
      return 0;
    }

    uint8_t flags () const {
      uint8_t flags = this->device().battery().low() ? 0x80 : 0x00;
      return flags;
    }
};

class UType : public MultiChannelDevice<Hal, MeasureChannel, NUM_CHANNELS, UList0> {
  public:
    typedef MultiChannelDevice<Hal, MeasureChannel, NUM_CHANNELS, UList0> TSDevice;
    UType(const DeviceInfo& info, uint16_t addr) : TSDevice(info, addr) {}
    virtual ~UType () {}

    virtual void configChanged () {
      TSDevice::configChanged();
      DPRINT(F("*LOW BAT Limit: "));
      DDECLN(this->getList0().lowBatLimit());
      this->battery().low(this->getList0().lowBatLimit());
      DPRINT(F("*Sendeintervall: ")); DDECLN(this->getList0().Sendeintervall());
    }
};

UType sdev(devinfo, 0x20);
ConfigButton<UType> cfgBtn(sdev);

void setup () {
  DINIT(57600, ASKSIN_PLUS_PLUS_IDENTIFIER);
  sdev.init(hal);
  buttonISR(cfgBtn, CONFIG_BUTTON_PIN);
  sdev.initDone();
}

void loop() {
  bool worked = hal.runready();
  bool poll = sdev.pollRadio();
  if ( worked == false && poll == false ) {
    hal.activity.savePower<Sleep<>>(hal);
  }
}

