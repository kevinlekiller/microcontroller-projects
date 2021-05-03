/**
 * Copyright (C) 2018  kevinlekiller
 * 
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 2 of the License, or
 * (at your option) any later version.
 * 
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 * 
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include <LiquidCrystal.h>
#include <Wire.h>
#include "IRremote.h"
#include "EmonLib.h"
#include <Adafruit_GFX.h>
#include <Adafruit_TFTLCD.h>
#include <TouchScreen.h>
#include "DHT.h"

#define DEBUG 1

#define DHTTYPE DHT22

const unsigned long compressorMaxTime = 1200000;
const unsigned long compressorGraceTime = 120000;
const unsigned long fanOffTime = 60000;

// Room temp needs to be at least this much higher than desired temp to start compressor
const float tempDifferentialHigh = 1.0;
// Room temp needs to be at least this much lower than desired temp for compressor to stop
const float tempDifferentialLow = 0.1;

const float thermistorCorrection = 2.5;

const float Vrms = 115.5;
const int IrmsSamples = 1480;
const float IrmsPostCorrection = 0.025;
const float IrmsPreCorrection = 53;

// Analog pins
const int thermistorPin = 15;
const int photoResistorPin = 14;
const int currentSensorPin = 13;
/*const int LCD_RD = A0;
const int LCD_WR = A1;
const int LCD_CD = A2;
const int LCD_CS = A3;
const int LCD_RS = A4;*/

// Digital pins
const int dhtPin = 35;
const int upSwitch = 52;
const int downSwitch = 53;
//                BS E  D4 D5  D6  D7
LiquidCrystal lcd(26, 27, 30, 31, 29, 28);

DHT dht(dhtPin, DHTTYPE);
IRsend irSend;
EnergyMonitor emon;

bool checkMillis(unsigned long, unsigned long, int);
void checkButtons();
void checkTemp();
void checkAC();
void checkCompressor();
void checkSleepMode();
void changeDesiredTemp(bool);
void compressorOn(short);
void compressorOff(short);
void unitOff();
short getPhotoResistor();
void getThermistorTemp();
void getDht();
void getIrms();
void printLcd();
void printSerial();

bool humidityMode = 0;
bool isCompressorOn = 0;
bool wantAC = 0;

unsigned long compressorTime;
unsigned long delayTimer = 0;
unsigned long buttonTime = 0;

float desiredTempReal = 25;
float desiredTemp = desiredTempReal;
float desiredHumidityReal = 50;
float desiredHumidity = desiredHumidityReal;
float roomTemp = 0.0;
float roomHumidity = 0.0;
double Irms = 0.0;

void setup()
{
  Serial.begin(9600);
  lcd.begin(16, 2);
  //Wire.begin();
  emon.current(currentSensorPin, IrmsPreCorrection);
  dht.begin();
  // Give time for Irms to settle.
  for (unsigned short i = 0; i < 5; i++ ){
    getIrms();
    delay(1000);
  }
  compressorTime = millis();
  pinMode(upSwitch, INPUT);
  pinMode(downSwitch, INPUT);
}

void loop()
{
  if (!checkMillis(buttonTime, 1000, 0)) {
    checkButtons();
  }
  if (!checkMillis(delayTimer, 2000, 0)) {
    //getThermistorTemp();
    getDht();
    //checkSleepMode();
    getIrms();
    printLcd();
    printSerial();
    checkCompressor();
    checkTemp();
    checkAC();
    delayTimer = millis();
  }
  delay(100);
}

bool checkMillis(unsigned long milliseconds, unsigned long difference, int debug = 1)
{
  if ((millis() - milliseconds) <= difference || millis() < milliseconds) {
    if (DEBUG && debug) {
      Serial.print("checkMillis() ");
      Serial.print((millis() - milliseconds));
      Serial.print("ms / ");
      Serial.println(difference); 
    }
    return 1;
  }
  return 0;
}

void checkButtons()
{
  if (digitalRead(upSwitch) == HIGH) {
    changeDesiredTemp(1);
  } else if (digitalRead(downSwitch) == HIGH) {
    changeDesiredTemp(0);
  }
}

void checkTemp()
{
  if (humidityMode) {
    if (roomHumidity && roomHumidity <= desiredHumidity) {
      humidityMode = 0;
      wantAC = 0;
      if (DEBUG) {
       Serial.println("Humidity mode off");
      }
    }
    return;
  }
  if (roomTemp >= (desiredTemp - 1) && roomHumidity && (roomHumidity - desiredHumidity) > 5) {
    humidityMode = 1;
    wantAC = 1;
    if (DEBUG) {
      Serial.println("Humidity mode on");
    }
    return;
  }
  if (isCompressorOn) {
    // 24.5 -  24.4 = 0.1 // 0.1 >= 0.1
    if ((desiredTemp - roomTemp) >= tempDifferentialLow) {
      wantAC = 0;
      return;
    }
  } else {
    // 26 - 24.5 = 1.5 // 1.5 >= 1.5
    if ((roomTemp - desiredTemp) >= tempDifferentialHigh) {
      wantAC = 1;
      if (DEBUG) {
        Serial.println("Room temperature is higher than desired temperature");
      }
      return;
    }
    wantAC = 0;
  }
}

void checkAC()
{
  if (wantAC) {
    if (Irms > 10) {
      /*bool irmsCheck = 0;
      for (unsigned short i = 0; i < 5; i++) {
        getIrms();
        if (Irms <= 10) {
          irmsCheck = 1;
          break;
        }
        delay(500);
      }*/
      if (1){//irmsCheck) {
        if (DEBUG) {
          Serial.println("Current draw too high, not turning on compressor.");
        }
        // Disable compressor if it's on
        unitOff();
        compressorTime = millis();
        return;
      }
    }
    if (!isCompressorOn) {
      // If compressor was started in last 5 mins, or after power failure, don't start (allows cap to charge)
      if (checkMillis(compressorTime, compressorGraceTime)) {
        if (DEBUG) {
          Serial.println("Compressor grace time");
        }
        return;
      }
      compressorOn(1);
      return;
    }
    if (!checkMillis(compressorTime, compressorMaxTime)) {
      if (DEBUG) {
        Serial.println("Compressor on too long, turning off");
      }
      compressorOff(1);
      delay(fanOffTime);
      unitOff();
    }
  } else if (isCompressorOn) {
    if (DEBUG) {
      Serial.println("Don't want compressor anymore, turning off");
    }
    compressorOff(1);
    delay(fanOffTime);
    unitOff();
  }
}

void checkCompressor()
{
  if (isCompressorOn && Irms < 1) {
    if (DEBUG) {
      Serial.println("Compressor is marked on but current is too low");
    }
    isCompressorOn = 0;
  } else if (!isCompressorOn && Irms > 2 && Irms < 10) {
    if (DEBUG) {
      Serial.println("Compressor is marked off but current is too high");
    }
    isCompressorOn = 1;
  }
}

void checkSleepMode()
{
  desiredTemp = desiredTempReal;
  return;
  if (getPhotoResistor() < 15) {
    desiredTemp = desiredTempReal + 0.5;
    desiredHumidity = desiredHumidityReal + 2.5;
  } else {
    desiredTemp = desiredTempReal;
    desiredHumidity = desiredHumidityReal;
  }
}

void changeDesiredTemp(bool direction)
{
  if (DEBUG) {
    Serial.print("Changed desired temperated from ");
    Serial.print(desiredTempReal);
  }
  if (direction && desiredTempReal < 30) {
    desiredTempReal += 0.5;
  }
  if (!direction && desiredTempReal > 16) {
    desiredTempReal -= 0.5;
  }
  if (DEBUG) {
    Serial.print(" to ");
    Serial.println(desiredTempReal);
  }
  desiredTemp = desiredTempReal;
  buttonTime = millis();
}

void compressorOn(short fanSpeed)
{
  if (DEBUG) {
    Serial.print("Enabling compressor with fan speed at ");
    Serial.println(fanSpeed);
  }
  switch (fanSpeed) {
    case 1:
      {
        unsigned int code[] = {
          8964, 4504, 652, 1652, 660, 544, 636, 1672, 656, 1648, 632, 1676, 656,
          548, 656, 548, 636, 568, 660, 548, 660, 544, 660, 548, 656, 548, 656,
          548, 660, 548, 656, 548, 656, 548, 660, 548, 656, 548, 656, 548, 656,
          548, 660, 544, 660, 1648, 656, 1652, 660, 540, 660, 548, 632, 576, 656,
          548, 660, 544, 660, 1648, 656, 548, 660, 1648, 656, 548, 660, 548, 656,
          1648, 660, 548, 656, 19964, 656, 548, 660, 544, 660, 548, 656, 548, 660,
          544, 660, 544, 660, 548, 656, 548, 660, 548, 656, 548, 656, 548, 660,
          544, 660, 548, 656, 1648, 660, 548, 656, 548, 656, 548, 660, 544, 656,
          552, 656, 548, 660, 548, 656, 548, 656, 548, 660, 548, 656, 548, 656,
          552, 652, 552, 656, 548, 656, 1648, 660, 544, 660, 544, 660, 1648, 660
        };
        irSend.sendRaw(code, sizeof(code) / sizeof(code[0]), 38);
      }
      break;
    case 2:
      {
        unsigned int code[] = {
          8952, 4516, 624, 1680, 632, 576, 644, 1660, 624, 1684, 620, 584, 624,
          1680, 624, 584, 648, 556, 624, 580, 628, 580, 648, 556, 648, 560, 624,
          580, 648, 556, 648, 560, 620, 584, 648, 556, 644, 564, 620, 584, 648, 556,
          624, 584, 620, 1684, 648, 1660, 620, 584, 624, 580, 648, 560, 624, 580, 648,
          556, 648, 1656, 628, 580, 620, 1688, 624, 580, 624, 580, 652, 1656, 644,
          560, 624, 19996, 648, 560, 648, 556, 648, 556, 624, 584, 644, 560, 648, 556,
          648, 560, 620, 584, 648, 556, 648, 560, 624, 580, 624, 584, 620, 584, 648,
          1660, 620, 584, 648, 556, 652, 556, 624, 584, 620, 580, 656, 556, 644, 560,
          644, 560, 648, 556, 648, 560, 620, 584, 648, 556, 624, 584, 648, 556, 624,
          1684, 644, 560, 624, 580, 624, 1684, 644
        };
        irSend.sendRaw(code, sizeof(code) / sizeof(code[0]), 38);
      }
      break;
    case 3:
      {
        unsigned int code[] = {
          8988, 4480, 632, 1672, 656, 552, 632, 1672, 660, 1648, 656, 1652, 656, 1648,
          656, 548, 660, 544, 656, 552, 656, 548, 660, 548, 656, 548, 656, 548, 660, 544,
          660, 548, 656, 552, 656, 544, 660, 548, 656, 548, 660, 544, 660, 548, 656, 1644,
          664, 1648, 656, 548, 660, 544, 660, 548, 656, 548, 656, 548, 660, 1648, 660, 544,
          656, 1648, 660, 548, 656, 548, 660, 1644, 660, 548, 656, 19968, 660, 540, 660,
          548, 660, 544, 660, 548, 656, 548, 660, 544, 660, 544, 660, 548, 656, 548, 660,
          544, 660, 544, 664, 544, 660, 544, 660, 1648, 656, 548, 660, 548, 660, 544, 656,
          548, 660, 544, 660, 548, 660, 544, 660, 548, 660, 544, 660, 544, 664, 544, 660,
          544, 660, 544, 660, 548, 656, 1648, 660, 544, 660, 548, 656, 1648, 660
        };
        irSend.sendRaw(code, sizeof(code) / sizeof(code[0]), 38);
      }
      break;
    case 4:
      {
        unsigned int code[] = {
          8984, 4484, 652, 1652, 656, 548, 656, 1648, 660, 1648, 656, 1648, 664, 1644, 660,
          548, 656, 548, 656, 548, 660, 544, 660, 544, 664, 544, 656, 548, 660, 548, 656,
          544, 660, 548, 656, 548, 660, 548, 656, 548, 660, 548, 656, 1648, 656, 1648, 660,
          1648, 656, 548, 660, 544, 656, 552, 656, 548, 660, 544, 660, 1644, 660, 548, 660,
          1644, 660, 544, 660, 548, 660, 1648, 656, 548, 660, 19964, 660, 544, 656, 552, 656,
          548, 660, 544, 660, 548, 660, 544, 660, 544, 660, 544, 660, 548, 660, 544, 660, 548,
          656, 552, 656, 544, 660, 1648, 660, 544, 660, 544, 660, 548, 660, 544, 660, 544,
          664, 544, 660, 544, 660, 544, 660, 548, 660, 544, 664, 544, 656, 548, 660, 544, 660,
          544, 664, 1644, 660, 544, 660, 548, 660, 1644, 660
        };
        irSend.sendRaw(code, sizeof(code) / sizeof(code[0]), 38);
      }
      break;
    case 0:
    default:
      {
        unsigned int code[] = {
          8984, 4492, 648, 1652, 652, 556, 648, 1656, 652, 1652, 652, 556, 648, 556, 652, 556,
          648, 556, 648, 556, 652, 556, 632, 572, 648, 556, 652, 552, 656, 552, 652, 552, 652, 556,
          648, 556, 652, 552, 652, 556, 624, 580, 652, 552, 652, 1656, 648, 1656, 652, 556, 648,
          556, 628, 584, 644, 556, 652, 552, 652, 1656, 652, 552, 652, 1652, 652, 556, 648, 552, 656,
          1652, 652, 552, 652, 19972, 652, 552, 652, 552, 652, 556, 652, 552, 652, 552, 652, 556, 652,
          556, 648, 556, 648, 556, 628, 576, 648, 560, 648, 556, 652, 552, 652, 1656, 648, 556, 648,
          556, 652, 552, 652, 552, 652, 556, 652, 556, 648, 556, 648, 556, 652, 552, 652, 552, 652,
          552, 656, 552, 652, 556, 648, 552, 652, 1656, 652, 556, 648, 556, 648, 1660, 648
        };
        irSend.sendRaw(code, sizeof(code) / sizeof(code[0]), 38);
      }
      break;
  }
  isCompressorOn = 1;
  compressorTime = millis();
}

void compressorOff(short fanSpeed)
{
  if (DEBUG) {
    Serial.print("Disabling compressor with fan speed at ");
    Serial.println(fanSpeed);
  }
  switch(fanSpeed) {
    case 1:
      {
        unsigned int code[] = {
          8964, 4504, 632, 1676, 628, 576, 656, 1648, 656, 1652, 656, 1648, 660, 548, 660, 544, 660,
          544, 660, 544, 656, 1652, 656, 1652, 656, 1648, 660, 544, 660, 548, 660, 544, 660, 548, 656,
          548, 660, 544, 660, 544, 660, 548, 660, 544, 660, 1648, 660, 1644, 660, 548, 660, 544, 660,
          544, 660, 544, 664, 544, 660, 1644, 660, 544, 664, 1640, 664, 548, 656, 548, 660, 1644, 660,
          544, 660, 19964, 660, 544, 660, 548, 660, 544, 660, 548, 660, 544, 660, 544, 660, 548, 660,
          544, 660, 544, 664, 544, 660, 548, 656, 548, 660, 544, 660, 1644, 664, 540, 688, 520, 660,
          548, 656, 544, 664, 544, 660, 544, 660, 544, 688, 520, 660, 544, 660, 548, 656, 548, 660, 544,
          660, 544, 664, 544, 660, 1644, 660, 1648, 656, 1648, 660, 548, 656
        };
        irSend.sendRaw(code, sizeof(code) / sizeof(code[0]), 38);
      }
      break;
    case 2:
      {
        unsigned int code[] = {
          8988, 4480, 632, 1676, 656, 548, 656, 1652, 656, 1648, 656, 552, 656, 1652, 652, 552, 652, 552,
          656, 548, 656, 1648, 656, 1652, 656, 1652, 656, 548, 656, 548, 660, 544, 660, 548, 656, 552, 656,
          548, 656, 548, 660, 544, 660, 548, 656, 1648, 656, 1648, 660, 548, 660, 544, 660, 544, 656, 552,
          656, 548, 656, 1648, 656, 552, 656, 1648, 660, 544, 660, 544, 660, 1648, 660, 544, 660, 19960, 664,
          544, 664, 540, 660, 544, 664, 544, 656, 548, 660, 548, 656, 548, 656, 548, 660, 548, 656, 548, 656,
          548, 660, 548, 656, 548, 660, 1644, 660, 548, 656, 548, 660, 544, 660, 548, 660, 544, 660, 544, 660,
          548, 660, 548, 652, 552, 656, 548, 660, 544, 664, 544, 660, 544, 656, 548, 660, 1644, 660, 1648,
          660, 1648, 656, 548, 656
        };
        irSend.sendRaw(code, sizeof(code) / sizeof(code[0]), 38);
      }
      break;
    case 3:
      {
        unsigned int code[] = {
          8964, 4496, 660, 1648, 656, 548, 660, 1648, 660, 1644, 660, 1648, 660, 1644, 660, 548, 660, 544, 660,
          544, 660, 1648, 660, 1644, 664, 1644, 656, 548, 656, 548, 660, 548, 684, 520, 660, 544, 660, 548, 656,
          548, 664, 540, 664, 544, 656, 1648, 660, 1648, 656, 548, 656, 548, 660, 548, 656, 548, 660, 544, 660,
          1648, 656, 548, 660, 1644, 660, 548, 660, 544, 660, 1644, 664, 544, 660, 19964, 660, 544, 660, 548, 656,
          548, 660, 548, 660, 544, 660, 544, 660, 544, 660, 548, 660, 544, 660, 548, 656, 544, 664, 544, 660, 544,
          660, 1648, 660, 544, 660, 548, 664, 540, 660, 548, 656, 548, 660, 544, 660, 548, 656, 548, 660, 544,
          664, 540, 660, 548, 660, 544, 660, 548, 660, 544, 660, 1648, 656, 1648, 660, 1644, 664, 544, 660
        };
        irSend.sendRaw(code, sizeof(code) / sizeof(code[0]), 38);
      }
      break;
    case 4:
      {
        unsigned int code[] = {
          8992, 4476, 660, 1644, 660, 548, 660, 1644, 660, 1648, 660, 1648, 656, 1648, 660, 544, 660, 548, 656, 544,
          664, 1648, 656, 1648, 660, 1644, 660, 544, 664, 544, 660, 544, 688, 516, 664, 544, 656, 548, 688, 520, 656,
          548, 660, 1648, 656, 1648, 660, 1648, 684, 520, 660, 544, 660, 544, 660, 548, 660, 544, 660, 1648, 660, 544,
          660, 1648, 656, 548, 664, 540, 684, 1624, 684, 520, 684, 19936, 684, 520, 664, 544, 684, 524, 672, 528, 664,
          544, 660, 544, 688, 516, 664, 548, 656, 544, 688, 520, 684, 520, 688, 516, 688, 520, 684, 1624, 656, 548, 660,
          544, 660, 544, 664, 540, 688, 520, 664, 540, 664, 544, 660, 544, 660, 548, 660, 544, 684, 524, 684, 516, 664,
          544, 684, 520, 664, 1644, 656, 1648, 684, 1624, 688, 520, 660
        };
        irSend.sendRaw(code, sizeof(code) / sizeof(code[0]), 38);
      }
      break;
    case 0:
    default:
      {
          unsigned int code[] = {8992, 4476, 660, 1644, 660, 548, 660, 1644, 660, 1648, 660, 1648, 656, 1648, 660, 544,
          660, 548, 656, 544, 664, 1648, 656, 1648, 660, 1644, 660, 544, 664, 544, 660, 544, 688, 516, 664, 544, 656, 548,
          688, 520, 656, 548, 660, 1648, 656, 1648, 660, 1648, 684, 520, 660, 544, 660, 544, 660, 548, 660, 544, 660, 1648,
          660, 544, 660, 1648, 656, 548, 664, 540, 684, 1624, 684, 520, 684, 19936, 684, 520, 664, 544, 684, 524, 672, 528,
          664, 544, 660, 544, 688, 516, 664, 548, 656, 544, 688, 520, 684, 520, 688, 516, 688, 520, 684, 1624, 656, 548, 660,
          544, 660, 544, 664, 540, 688, 520, 664, 540, 664, 544, 660, 544, 660, 548, 660, 544, 684, 524, 684, 516, 664, 544,
          684, 520, 664, 1644, 656, 1648, 684, 1624, 688, 520, 660
        };
        irSend.sendRaw(code, sizeof(code) / sizeof(code[0]), 38);
      }
      break;
  }
  humidityMode = 0;
  isCompressorOn = 0;
  compressorTime = millis();
}

void unitOff()
{
  unsigned int code[] = {
    8988, 4504, 628, 1676, 632, 572, 632, 1676, 632, 572, 632, 572, 632, 572, 632, 576, 632, 572, 632, 576, 632, 572, 632,
    572, 632, 572, 632, 576, 632, 572, 632, 576, 628, 576, 656, 548, 636, 568, 632, 576, 628, 576, 656, 552, 628, 1676, 632,
    568, 636, 572, 632, 572, 636, 572, 632, 576, 628, 576, 632, 1676, 632, 572, 656, 1648, 636, 568, 636, 572, 632, 1672,
    632, 576, 632, 19992, 652, 552, 656, 552, 632, 572, 632, 572, 632, 572, 636, 572, 632, 576, 628, 576, 632, 572, 632,
    572, 632, 572, 632, 576, 656, 552, 652, 1652, 632, 572, 632, 576, 632, 572, 628, 580, 652, 552, 632, 572, 632, 572,
    656, 552, 632, 572, 632, 572, 656, 552, 632, 572, 656, 552, 628, 576, 656, 1648, 632, 572, 636, 572, 632, 572, 632
  };
  irSend.sendRaw(code, sizeof(code) / sizeof(code[0]), 38);
  humidityMode = 0;
  isCompressorOn = 0;
}

short getPhotoResistor()
{
  return analogRead(photoResistorPin);
}

void getDht()
{
  float h = dht.readHumidity();
  roomHumidity = isnan(h) ? 0 : h;
  float t = dht.readTemperature();
  if (isnan(t)) {
    getThermistorTemp();
  }
  if (!roomHumidity || isnan(t)) {
    return; 
  }
  roomTemp = t;
  //float i = dht.computeHeatIndex(t,h,false);
  if (DEBUG) {
    Serial.print("DHT: Humidity: ");
    Serial.print(h);
    Serial.print("% Temperature: ");
    Serial.print(t);
    //Serial.print("C Humidex: ");
    //Serial.print(i);
    //Serial.println("*C");
  }
}

void getThermistorTemp()
{
    float tempK = log(10000.0 * ((1024.0 / analogRead(thermistorPin) - 1)));
    roomTemp = (1 / (0.001129148 + (0.000234125 + (0.0000000876741 * tempK * tempK )) * tempK )) - 273.15 - thermistorCorrection;
}

void getIrms()
{
  // power = Irms * Vrms
  Irms = emon.calcIrms(IrmsSamples);
  Irms -= IrmsPostCorrection;
  Irms = Irms >= 0 ? Irms : 0;
}


void printSerial()
{
  Serial.print("Temperature: ");
  Serial.print(roomTemp);
  Serial.print("C    Wanted Temp: ");
  Serial.print(desiredTemp);
  //Serial.print("C    Photoresitor: ");
  //Serial.print(getPhotoResistor());
  Serial.print("C    Power: ");
  Serial.print(Irms*Vrms);
  Serial.print("W    Amperage: ");
  Serial.print(Irms);
  Serial.print("A   Comp ");
  Serial.print(isCompressorOn);
  Serial.print("   WantAC ");
  Serial.print(wantAC);
  if (desiredTemp > desiredTempReal) {
    Serial.print("    Sleepmode on");
  }
  Serial.println("");
}

void printLcd()
{
  lcd.setCursor(0, 0);
  /////////("01234567890123456
  /////////("R:23.55 W:23.42");
  lcd.print("R:      W:");
  lcd.setCursor(2, 0);
  lcd.print(roomTemp);
  lcd.setCursor(10,0);
  lcd.print(desiredTempReal);
  lcd.setCursor(0,1);
  lcd.print("C:      P:     W");
  lcd.setCursor(3,1);
  lcd.print(isCompressorOn ? 1 : 0);
  lcd.setCursor(11,1);
  lcd.print((int)(Irms*Vrms));
  
}
