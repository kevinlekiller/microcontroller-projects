/**
   Copyright (C) 2018-2019  kevinlekiller

   This program is free software: you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation, either version 2 of the License, or
   (at your option) any later version.

   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with this program.  If not, see <http://www.gnu.org/licenses/>.
*/

#include <OneWire.h> 
#include <DallasTemperature.h>
#include <LiquidCrystal.h>
#include "IRLremote.h"

//#define DEBUG

/* AC Side | Arduino Side | Function
   blue    | white        | compressor
   orange  | orange       | fan low
   black   | black        | ground
   brown   | red          | fan med
   green   | yellow       | fan high */
#define RELAYS 4
#define FANL 0 // low speed
#define FANM 1 // med speed
#define FANH 2 // high speed
#define FANSPEEDS 3
#define COMPRESSOR 3

#define IRPIN 18
#define LCDLEDPIN 4
#define LCDCONPIN 2
#define ONE_WIRE_BUS 48
#define LCDBRIGHTNESS 10
#define LCDCONTRAST 100
#define LCDTIME 10000
#define TEMPTIME 1000

typedef struct {
  const short relay;
  const short pin;
} relayArr;

relayArr relays[RELAYS] = {
  {FANL, 53},
  {FANM, 50},
  {FANH, 51},
  {COMPRESSOR, 52}
};

const unsigned short LOOPDELAY = 500;
const unsigned short LOGICDLEAY = 2000;
const unsigned long COMPMAXTIME = 1200000;
const unsigned long COMPGRACETIME = 120000;
const unsigned long FANOFFTIME = 60000;
const unsigned long FANONTIME = 20000;
// Room temp needs to be at least this much higher than desired temp to start compressor
const float tempDifferentialHigh = 1.0;
// Room temp needs to be at least this much lower than desired temp for compressor to stop
const float tempDifferentialLow = 0.1;

bool checkMillis(unsigned long, unsigned long, bool);
void checkIR();
void checkTemp();
void checkAC();
void changeDesiredTemp(bool);
void unitOff();
void getTemp();
#ifdef DEBUG
void printSerial();
#endif
void toggleUnit();
void toggleAC();
void toggleFanSpeed(bool);
void toggleRelay(short, bool);

bool unitOn = 0;
bool wantAC = 0;

unsigned long compressorTime;
unsigned long delayTimer = 0;
unsigned long lcdTime = 0;
unsigned long fanTime = 0;
unsigned long tempTime = 0;

unsigned short fanSpeed = 0;

float desiredTemp = 25.0;
float roomTemp = 0.0;

CNec IRLremote;
LiquidCrystal lcd(7, 8, 9, 10, 11, 12);
OneWire oneWire(ONE_WIRE_BUS);
DallasTemperature tempSensor(&oneWire);

void setup()
{
#ifdef DEBUG
  Serial.begin(9600);
#endif
  compressorTime = millis();
  for (int i = 0; i < RELAYS; i++) {
    pinMode(relays[i].pin, OUTPUT);
  }
  pinMode(LED_BUILTIN, OUTPUT);
  digitalWrite(LED_BUILTIN, LOW);
  pinMode(LCDLEDPIN, OUTPUT);
  pinMode(LCDCONPIN, OUTPUT);
  IRLremote.begin(IRPIN);
  lcd.begin(16, 2);
  lcd.noDisplay();
  analogWrite(LCDCONPIN, LCDCONTRAST);
  tempSensor.begin();
  getTemp();
}

bool checkMillis(unsigned long milliseconds, unsigned long difference, bool debug = 0)
{
  if ((millis() - milliseconds) <= difference || millis() < milliseconds) {
#ifdef DEBUG
    if (debug) {
      Serial.print("checkMillis() ");
      Serial.print((millis() - milliseconds));
      Serial.print("ms / ");
      Serial.println(difference);
    }
#endif
    return 1;
  }
  return 0;
}

void loop()
{
  if (!checkMillis(tempTime, TEMPTIME)) {
    getTemp();
  }
  checkIR();
  if (lcdTime && !checkMillis(lcdTime, LCDTIME)) {
    lcdTime = 0;
    analogWrite(LCDLEDPIN, 0);
    lcd.noDisplay();
  }
  if (fanTime && !checkMillis(fanTime, FANOFFTIME)) {
    fanTime = 0;
    for (int i = 0; i < FANSPEEDS; i++) {
      toggleRelay(relays[i].relay, 0);
    }
  }
  if (unitOn && !checkMillis(delayTimer, LOGICDLEAY)) {
    #ifdef DEBUG
      printSerial();
    #endif
    checkTemp();
    checkAC();
    delayTimer = millis();
  }
  delay(LOOPDELAY);
}

void checkIR()
{
  if (!IRLremote.available()) {
    return;
  }
  auto data = IRLremote.read();
  if (data.address != 0xFF00) {
    return;
  }
  switch (data.command) {
    case 0x45: // power
      toggleUnit();
      break;
    case 0x9: // up
      changeDesiredTemp(1);
      break;
    case 0x7: // down
      changeDesiredTemp(0);
      break;
    case 0x42: // 7
      lcd.begin(16, 2);
      break;
    case 0x43: // fast forward6
      toggleFanSpeed(1);
      break;
    case 0x44: // rewind
      toggleFanSpeed(0);
      break;
    case 0x4A: // 8
      if (!unitOn) {
        toggleRelay(fanSpeed, 1);
      }
      break;
    case 0x52: // 9
      if (!unitOn) {
        toggleRelay(fanSpeed, 0);
      }
      break;
    case 0xD: // st/rept
      analogWrite(LCDLEDPIN, LCDBRIGHTNESS);
      lcd.display();
      lcd.clear();
      lcd.print("RM");
      lcd.setCursor(3, 0);
      lcd.print(roomTemp);
      lcd.setCursor(8, 0);
      lcd.print("C  Fan");
      lcd.setCursor(15, 0);
      lcd.print((digitalRead(relays[fanSpeed].pin) == LOW) ? 0 : fanSpeed + 1);
      lcd.setCursor(0, 1);
      lcd.print("AC ");
      lcd.setCursor(3, 1);
      lcd.print(desiredTemp);
      lcd.setCursor(8, 1);
      lcd.print("C  Cmp");
      lcd.setCursor(15, 1);
      lcd.print(digitalRead(relays[COMPRESSOR].pin));
      lcdTime = millis();
      break;
  }
  #ifdef DEBUG
    Serial.print("RAW IR code received: 0x");
    Serial.print(data.address, HEX);
    Serial.println(data.command, HEX);
  #endif
}

void toggleUnit()
{
#ifdef DEBUG
  Serial.print("Compressor / Fan control ");
  Serial.print(unitOn ? "disabled" : "enabled");
  Serial.println(" until power button is pressed again.");
#endif
  analogWrite(LCDLEDPIN, LCDBRIGHTNESS);
  lcd.clear();
  lcd.display();
  if (unitOn) {
    lcd.print("AC turned off.");
    unitOff();
  } else {
    lcd.print("AC turned on.");
    unitOn = 1;
  }
  lcdTime = millis();
}

void toggleRelay(short relay, bool on)
{
#ifdef DEBUG
  const char * rType = (relay == COMPRESSOR) ? "compressor" : "fan";
#endif
  if ((on && digitalRead(relays[relay].pin) == HIGH) || (!on && digitalRead(relays[relay].pin) == LOW)) {
#ifdef DEBUG
    Serial.print("Not toggling ");
    Serial.print(rType);
    Serial.println(", already in proper state.");
#endif
    return;
  }
#ifdef DEBUG
  Serial.print("Turning ");
  Serial.print(rType);
  Serial.println(on ? " on." : " off");
#endif
  digitalWrite(relays[relay].pin, (on ? HIGH : LOW));
}

void checkTemp()
{
  if (digitalRead(relays[COMPRESSOR].pin) == HIGH) {
    // 24.5 -  24.4 = 0.1 // 0.1 >= 0.1
    if ((desiredTemp - roomTemp) >= tempDifferentialLow) {
      wantAC = 0;
      return;
    }
  } else {
    // 26 - 24.5 = 1.5 // 1.5 >= 1.5
    if ((roomTemp - desiredTemp) >= tempDifferentialHigh) {
      wantAC = 1;
#ifdef DEBUG
      Serial.println("Room temperature is higher than desired temperature");
#endif
      return;
    }
    wantAC = 0;
  }
}

void checkAC()
{
  if (wantAC) {
    if (digitalRead(relays[COMPRESSOR].pin) == LOW) {
      // If compressor was started in last 5 mins, or after power failure, don't start (allows cap to charge)
      if (checkMillis(compressorTime, COMPGRACETIME, 1)) {
#ifdef DEBUG
        Serial.println("Compressor grace time");
#endif
        return;
      }
      toggleAC(1);
      return;
    }
    if (!checkMillis(compressorTime, COMPMAXTIME, 1)) {
#ifdef DEBUG
      Serial.println("Compressor on too long, turning off");
#endif
      toggleAC(0);
    }
  } else if (digitalRead(relays[COMPRESSOR].pin) == HIGH) {
#ifdef DEBUG
    Serial.println("Don't want compressor anymore, turning off");
#endif
    toggleAC(0);
  }
}

void changeDesiredTemp(bool direction)
{
#ifdef DEBUG
  Serial.print("Changed desired tmeprature from ");
  Serial.print(desiredTemp);
#endif
  float oldTemp = desiredTemp;
  if (direction && desiredTemp < 30) {
    desiredTemp += 0.5;
  }
  if (!direction && desiredTemp > 16) {
    desiredTemp -= 0.5;
  }
#ifdef DEBUG
  Serial.print(" to ");
  Serial.println(desiredTemp);
#endif
  analogWrite(LCDLEDPIN, LCDBRIGHTNESS);
  lcd.clear();
  lcd.display();
  lcd.print("Old Temp:");
  lcd.setCursor(10, 0);
  lcd.print(oldTemp);
  lcd.setCursor(15, 0);
  lcd.print("C");
  lcd.setCursor(0, 1);
  lcd.print("New Temp:");
  lcd.setCursor(10, 1);
  lcd.print(desiredTemp);
  lcd.setCursor(15, 1);
  lcd.print("C");
  lcdTime = millis();
}

void toggleAC(bool on)
{
#ifdef DEBUG
  Serial.print(on ? "Starting" : "Stopping");
  Serial.println(" Air Conditioner");
#endif
  if (on) {
    toggleRelay(fanSpeed, on);
  } else {
    fanTime = millis();
  }
  toggleRelay(COMPRESSOR, on);
  compressorTime = millis();
}

void toggleFanSpeed(bool up)
{
  unsigned short oldFanSpeed = fanSpeed;
  if (up && fanSpeed < FANH) {
    fanSpeed++;
  }
  if (!up && fanSpeed > FANL) {
    fanSpeed--;
  }
  if (oldFanSpeed == fanSpeed) {
#ifdef DEBUG
    Serial.println("Not changing fan speed, same fan speed requested.");
#endif
    return;
  }
  if (digitalRead(relays[oldFanSpeed].pin) == HIGH) {
    digitalWrite(relays[oldFanSpeed].pin, LOW);
    digitalWrite(relays[fanSpeed].pin, HIGH);
  }
#ifdef DEBUG
  Serial.print("Changed fan speed from ");
  Serial.print(oldFanSpeed);
  Serial.print(" to ");
  Serial.println(fanSpeed);
#endif
  analogWrite(LCDLEDPIN, LCDBRIGHTNESS);
  lcd.clear();
  lcd.display();
  lcd.print("Old fan speed:");
  lcd.setCursor(15, 0);
  lcd.print(oldFanSpeed);
  lcd.setCursor(0, 1);
  lcd.print("New fan speed:");
  lcd.setCursor(15, 1);
  lcd.print(fanSpeed);
  lcd.setCursor(0, 1);
  lcdTime = millis();
}

void unitOff()
{
  toggleRelay(COMPRESSOR, 0);
  compressorTime = millis();
  fanTime = millis();
  unitOn = 0;
}

void getTemp()
{
  tempSensor.requestTemperatures();
  roomTemp = tempSensor.getTempCByIndex(0);
}

#ifdef DEBUG
void printSerial()
{
  Serial.print("Temperature: ");
  Serial.print(roomTemp);
  Serial.print("C    Wanted Temp: ");
  Serial.print(desiredTemp);
  Serial.print("C   Comp ");
  Serial.print(digitalRead(relays[COMPRESSOR].pin));
  Serial.print("   WantAC ");
  Serial.print(wantAC);
  Serial.print("   Fan " );
  Serial.print((digitalRead(relays[fanSpeed].pin) == LOW) ? "off" : "on");
  Serial.print(" (speed ");
  Serial.print(fanSpeed);
  Serial.println(")");
}
#endif
