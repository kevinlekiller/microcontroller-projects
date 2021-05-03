/**
   Copyright (C) 2018-2021  kevinlekiller

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
#include <WiFi.h>
#include <WebServer.h>

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

#define LED_BUILTIN 2
#define ONE_WIRE_BUS 27 // Pin 27 of the ESP32 for the DS18B20 temp sensor.
#define TEMPTIME 1000

typedef struct {
  const short relay;
  const short pin;
} relayArr;

relayArr relays[RELAYS] = { // Pins for the relays
  {FANL, 19},
  {FANM, 18},
  {FANH, 17},
  {COMPRESSOR, 16}
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

const char* SSID = "ssid";
const char* WPAPASS = "key";

bool checkMillis(unsigned long, unsigned long, bool);
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
void checkWiFi();
void webBase();
void webAcPower();
void webTempUp();
void webTempDown();
void webFanUp();
void webFanDown();
String formatHtml();

bool unitOn = 0;
bool wantAC = 0;

unsigned long compressorTime;
unsigned long delayTimer = 0;
unsigned long fanTime = 0;
unsigned long tempTime = 0;

unsigned short fanSpeed = 0;

float desiredTemp = 25.0;
float roomTemp = 0.0;

OneWire oneWire(ONE_WIRE_BUS);
DallasTemperature tempSensor(&oneWire);
WebServer server(80);

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
  tempSensor.begin();
  getTemp();
  checkWiFi();

  server.on("/", webBase);
  server.on("/power", webAcPower);
  server.on("/tempup", webTempUp);
  server.on("/tempdown", webTempDown);
  server.on("/fanup", webFanUp);
  server.on("/fandown", webFanDown);
  server.begin();
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
  checkWiFi();
  if (WiFi.status() == WL_CONNECTED) {
    server.handleClient();
  }
  delay(LOOPDELAY);
}

void checkWiFi()
{
  if (WiFi.status() == WL_CONNECTED) {
    return;
  }
  for (int i = 0; i < 5; i++) {
    if (WiFi.begin(SSID, WPAPASS) == WL_CONNECTED) {
      break;
    }
    delay(2000);
  }
}

void webBase()
{
  server.send(200, "text/html", formatHtml()); 
}

void webAcPower()
{
  toggleUnit();
  server.send(200, "text/html", formatHtml()); 
}

void webTempUp()
{
  changeDesiredTemp(1);
  server.send(200, "text/html", formatHtml()); 
}

void webTempDown()
{
  changeDesiredTemp(0);
  server.send(200, "text/html", formatHtml()); 
}

void webFanUp()
{
  toggleFanSpeed(1);
  server.send(200, "text/html", formatHtml()); 
}

void webFanDown()
{
  toggleFanSpeed(0);
  server.send(200, "text/html", formatHtml()); 
}

String formatHtml()
{
  String fanSpd = "<b style=\"color: ";
  switch (fanSpeed) {
    case 1:
      fanSpd += "orange\">Medium";
      break;
    case 2:
      fanSpd += "red\">High";
      break;
    default:
    case 0:
      fanSpd += "green\">Low";
      break;
  }
  fanSpd += "</b>";
  String compStatus;
  if (digitalRead(relays[COMPRESSOR].pin) == LOW) {
    compStatus = checkMillis(compressorTime, COMPGRACETIME, 0) ? "Compressor on cooldown for " + (String) ((COMPGRACETIME - (millis() - compressorTime)) / 1000) + " secs." : "Compressor ready to run.";
  } else {
    compStatus = "Compressor running for " + (String) ((millis() - compressorTime) / 1000) + " secs. (Max " + (String) (COMPMAXTIME / 1000) + " secs.)";
  }
  String html = "<!DOCTYPE html>\n";
  html += "<html lang=\"en-US\">\n";
  html += "  <head>\n";
  html += "    <meta charset=\"UTF-8\">\n";
  html += "    <meta name=\"viewport\" content=\"width=device-width, initial-scale=1\">\n";
  html += "    <style>a:link {color: lightblue;} a:visited {color: violet;} a:hover {color: red;}</style>\n";
  html += "    <style>body {background: #333; text-align: center;}</style>\n";
  html += "    <title>ESP32 Air Conditioner</title\n";
  html += "  </head>\n";
  html += "  <body text=\"white\">\n";
  html += "    <h2><a href=\"/\">ESP32 Air Conditioner</a></h2>\n";
  html += "    <p><a href=\"power\">Power</a>: <b style=\"color: " + (String) (unitOn ? "green\">On" : "red\">Off") + "</b></p>\n";
  html += "    <p>Fan Speed: " + fanSpd + " <a href=\"fanup\">[UP]</a> <a href=\"fandown\">[DOWN]</a></p>\n";
  html += "    <p>Desired Temperature: <b style=\"color: green\">" + (String) desiredTemp + "C</b> <a href=\"tempup\">[UP]</a> <a href=\"tempdown\">[DOWN]</a></p>\n";
  html += "    <p>Room Temperature: <b style=\"color: green\">" + (String) roomTemp + "C</b></p>\n";
  html += "    <p>Compressor: <b style=\"color: " + (String) (digitalRead(relays[COMPRESSOR].pin) == HIGH ? "green\">On" : "red\">Off") + "</b></p>\n";
  html += "    <p>Compressor Status: " + compStatus + "</p>\n";
  html += "  </body>\n";
  html += "</html>\n";
  return html;
}

void toggleUnit()
{
#ifdef DEBUG
  Serial.print("Compressor / Fan control ");
  Serial.print(unitOn ? "disabled" : "enabled");
  Serial.println(" until power button is pressed again.");
#endif
  if (unitOn) {
    unitOff();
  } else {
    unitOn = 1;
  }
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