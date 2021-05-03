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

const int pwmPin = 8;
const int relayPin = 9;
const int thermistorPin = A0;

const int initialFanSpeed = 45;

const unsigned long compressorMaxTime = 1800000;
const unsigned long compressorGraceTime = 180000;

const float diffTemp = 3;
const float absMinTemp = 3;

unsigned long compressorTime;
unsigned long extraGraceTime = 0;
bool isCompressorOn = 0;

float offTemp = absMinTemp;
float onTemp = offTemp + diffTemp;
float temperature, floorTemp, ceilTemp, floorTempCur, ceilTempCur;

void compressorOff();
 
void setup(void)
{
  Serial.begin(9600);
  TCCR4A = TCCR4B = TCNT4 = 0;
  ICR4   = (F_CPU / 25000) / 2;
  OCR4C  = ICR4 / 2;
  TCCR4A = _BV(COM4C1) | _BV(WGM41);
  TCCR4B = _BV(WGM43)  | _BV(CS40);
  pinMode(pwmPin, OUTPUT);
  pinMode(relayPin, OUTPUT);
  compressorTime = millis();
  floorTemp = floorTempCur = 99;
  OCR4C = initialFanSpeed;
}

void loop(void)
{
  unsigned long curCompTime = millis() - compressorTime;

  temperature = 0;
  for (uint8_t i = 0; i < 5; i++) {
    temperature += analogRead(thermistorPin);
    delay(10);
  }
  temperature = 10000 / (1023 / (temperature / 5) - 1);
  temperature = log(temperature / 10000) / 3950;
  temperature += 1.0 / (25 + 273.15);
  temperature = (1.0 / temperature) - 273.15;
  if (millis() < compressorTime) {
    compressorOff();
  } else if (curCompTime >= compressorGraceTime + extraGraceTime) {
    extraGraceTime = 0;
    if (!isCompressorOn) {
      if (temperature > offTemp + diffTemp) {
        if (floorTempCur != 99 && floorTempCur > offTemp + 0.1) {
          offTemp += 0.1;
        }
        compressorTime = millis();
        ceilTempCur = 0;
        floorTempCur = 99;
        isCompressorOn = 1;
        OCR4C = initialFanSpeed * 4;
        Serial.println("Compressor on.");
        digitalWrite(relayPin, HIGH);
      }
    } else if (curCompTime >= compressorMaxTime) {
      compressorOff();
      Serial.println("Sleeping longer to let compressor cool off");
      extraGraceTime = compressorGraceTime;
    } else if (millis() < compressorTime || temperature < offTemp) {
      compressorOff();
    }
  } else if (!isCompressorOn) {
    Serial.print("Grace time: ");
    Serial.print(curCompTime / 1000);
    Serial.print("/");
    Serial.print((compressorGraceTime + extraGraceTime) / 1000);
    Serial.print(" ");
  }

  ceilTempCur = temperature > ceilTempCur ? temperature : ceilTempCur;
  floorTempCur = temperature < floorTempCur ? temperature : floorTempCur;
  ceilTemp = temperature > ceilTemp ? temperature : ceilTemp;
  floorTemp = temperature < floorTemp ? temperature : floorTemp;
 
  Serial.print("Temperature "); 
  Serial.print(temperature);
  Serial.print(" *C Wanted Low: ");
  Serial.print(offTemp);
  Serial.print(" *C Wanted High: ");
  Serial.print(offTemp + diffTemp);
  Serial.print(" *C Max Low: ");
  Serial.print(floorTemp);
  Serial.print(" *C Max High: ");
  Serial.print(ceilTemp);
  Serial.print(" *C Cur Low: ");
  Serial.print(floorTempCur);
  Serial.print(" *C Cur High: ");
  Serial.print(ceilTempCur);
  Serial.print(" *C Compressor time: ");
  Serial.println(curCompTime / 1000);

  if (floorTempCur <= offTemp && offTemp > absMinTemp) {
    offTemp -= 0.05;
    onTemp = offTemp + diffTemp;
  }
 
  delay(1000);
}

void compressorOff()
{
  isCompressorOn = 0;
  OCR4C = initialFanSpeed;
  Serial.println("Compressor off.");
  digitalWrite(relayPin, LOW);
  compressorTime = millis();
  extraGraceTime = compressorGraceTime / 2;
}
