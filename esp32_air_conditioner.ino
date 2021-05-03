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
#define TEMPTIME 1000 // Delay between checking temperature sensor.

typedef struct {
  const short relay;
  const short pin;
} relayArr;

relayArr relays[RELAYS] = { // Pins for the relays.
  {FANL, 19},
  {FANM, 18},
  {FANH, 17},
  {COMPRESSOR, 16}
};

const unsigned short LOOPDELAY = 500; // Main loop delay.
const unsigned short LOGICDLEAY = 2000; // Delay between check compressor / fan status.
const unsigned long COMPMAXTIME = 1200000; // Max amount of milliseconds compressor can run.
const unsigned long COMPGRACETIME = 120000; // Amount of milliseconds to wait between cycling compressor.
const unsigned long FANOFFTIME = 60000; // Amount of milliseconds to keep fan on after turning off compressor.
// Min and max values for temp differentials.
const float minTempDiff = 0.25;
const float maxTempDiff = 2.0;

const char* SSID = "ssid";
const char* WPAPASS = "pass";

bool checkMillis(unsigned long, unsigned long, bool);
void checkTemp();
void checkAC();
void changeDesiredTemp(bool);
void getTemp();
void toggleUnit();
void changeDiff(bool, bool);
void toggleAC();
void toggleFanSpeed(bool);
void toggleRelay(short, bool);
void checkWiFi();
void webProcess();
String formatHtml();

bool unitOn = 0;
bool wantAC = 0;

unsigned long compressorTime; // Current runtime for the compressor.
unsigned long prevCompressorTime = 0; // Previous runtime for the compressor.
unsigned long averageCompressorTime = 0; // Average compressor time since AC turned on.
unsigned long delayTimer = 0;
unsigned long fanTime = 0;
unsigned long tempTime = 0;

unsigned short fanSpeed = 0;

float desiredTemp = 25.0;
float roomTemp = 0.0;
// Room temp needs to be at least this much higher than desired temp to start compressor
float tempDifferentialHigh = 0.5;
// Room temp needs to be at least this much lower than desired temp for compressor to stop
float tempDifferentialLow = 0.5;

OneWire oneWire(ONE_WIRE_BUS);
DallasTemperature tempSensor(&oneWire);
WebServer server(80);

void setup()
{
  compressorTime = millis();
  for (int i = 0; i < RELAYS; i++) {
    pinMode(relays[i].pin, OUTPUT);
  }
  pinMode(LED_BUILTIN, OUTPUT);
  digitalWrite(LED_BUILTIN, LOW);
  tempSensor.begin();
  getTemp();
  checkWiFi();

  server.on("/", webProcess);
  server.on("/power", webProcess);
  server.on("/tempup", webProcess);
  server.on("/tempdown", webProcess);
  server.on("/fanup", webProcess);
  server.on("/fandown", webProcess);
  server.on("/hdiffup", webProcess);
  server.on("/hdiffdown", webProcess);
  server.on("/ldiffup", webProcess);
  server.on("/ldiffdown", webProcess);
  server.onNotFound(webProcess);
  server.begin();
}

bool checkMillis(unsigned long milliseconds, unsigned long difference, bool debug = 0)
{
  if ((millis() - milliseconds) <= difference || millis() < milliseconds) {
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

void webProcess()
{
  if (server.uri().endsWith("/power")) {
    toggleUnit();
  } else if (server.uri().endsWith("/tempup")) {
    changeDesiredTemp(1);
  } else if (server.uri().endsWith("/tempdown")) {
    changeDesiredTemp(0);
  } else if (server.uri().endsWith("/fanup")) {
    toggleFanSpeed(1);
  } else if (server.uri().endsWith("/fandown")) {
    toggleFanSpeed(0);
  } else if (server.uri().endsWith("/hdiffup")) {
    changeDiff(1, 1);
  } else if (server.uri().endsWith("/hdiffdown")) {
    changeDiff(0, 1);
  } else if (server.uri().endsWith("/ldiffup")) {
    changeDiff(0, 0);
  } else if (server.uri().endsWith("/ldiffdown")) {
    changeDiff(1, 0);
  }
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
  html += "    <p>Compressor turns on at:  <b style=\"color: green\">" + (String) (desiredTemp + tempDifferentialHigh) +  "C</b> <a href=\"hdiffup\">[UP]</a> <a href=\"hdiffdown\">[DOWN]</a></p>\n";
  html += "    <p>Compressor turns off at:  <b style=\"color: green\">" + (String) (desiredTemp - tempDifferentialLow) +  "C</b> <a href=\"ldiffup\">[UP]</a> <a href=\"ldiffdown\">[DOWN]</a></p>\n";
  html += "    <p>Room Temperature: <b style=\"color: green\">" + (String) roomTemp + "C</b></p>\n";
  html += "    <p>Compressor: <b style=\"color: " + (String) (digitalRead(relays[COMPRESSOR].pin) == HIGH ? "green\">On" : "red\">Off") + "</b></p>\n";
  html += "    <p>Compressor Status: " + compStatus + "</p>\n";
  if (prevCompressorTime > 0 ){
    html += "    <p>Last compressor runtime: " + (String) (prevCompressorTime / 1000) + " seconds.</p>\n";
    html += "    <p>Average compressor runtime: " + (String) (averageCompressorTime / 1000) + " seconds.</p>\n";
  }
  html += "  </body>\n";
  html += "</html>\n";
  return html;
}

void toggleUnit()
{
  if (unitOn) {
    if (digitalRead(relays[COMPRESSOR].pin) == HIGH) {
      toggleRelay(COMPRESSOR, 0);
      compressorTime = millis();
    }
    fanTime = millis();
    unitOn = 0;
  } else {
    unitOn = 1;
  }
}

void toggleRelay(short relay, bool on)
{
  if ((on && digitalRead(relays[relay].pin) == HIGH) || (!on && digitalRead(relays[relay].pin) == LOW)) {
    return;
  }
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
        return;
      }
      toggleAC(1);
      return;
    }
    if (!checkMillis(compressorTime, COMPMAXTIME, 1)) {
      toggleAC(0);
    }
  } else if (digitalRead(relays[COMPRESSOR].pin) == HIGH) {
    toggleAC(0);
  }
}

void changeDesiredTemp(bool direction)
{
  float oldTemp = desiredTemp;
  if (direction && desiredTemp < 30) {
    desiredTemp += 0.5;
  }
  if (!direction && desiredTemp > 16) {
    desiredTemp -= 0.5;
  }
}

void changeDiff(bool direction, bool high)
{
  if (high) {
    if (direction && tempDifferentialHigh < maxTempDiff) {
      tempDifferentialHigh += 0.1;
    } else if (!direction && tempDifferentialHigh > minTempDiff) {
      tempDifferentialHigh -= 0.1;
    }
  } else {
    if (direction && tempDifferentialLow < maxTempDiff) {
      tempDifferentialLow += 0.1;
    } else if (!direction && tempDifferentialLow > minTempDiff) {
      tempDifferentialLow -= 0.1;
    }
  }
}

void toggleAC(bool on)
{
  if (on) {
    toggleRelay(fanSpeed, on);
  } else {
    prevCompressorTime = millis() - compressorTime;
    averageCompressorTime = averageCompressorTime == 0 ? prevCompressorTime : (unsigned long) ((prevCompressorTime + averageCompressorTime) / 2);
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
    return;
  }
  if (digitalRead(relays[oldFanSpeed].pin) == HIGH) {
    digitalWrite(relays[oldFanSpeed].pin, LOW);
    digitalWrite(relays[fanSpeed].pin, HIGH);
  }
}

void getTemp()
{
  tempSensor.requestTemperatures();
  roomTemp = tempSensor.getTempCByIndex(0);
}
