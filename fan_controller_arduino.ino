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

//reading fan tach https://forum.arduino.cc/index.php?topic=181115.0
//setting fan pwm https://forum.arduino.cc/index.php?topic=512516.0

/* Reads CPU temp from serial interface, sets fan speed based on CPU temp, turn off fan(s) if temp below threshold.
   Monitors fan RPM, but does nothing with it, only shows it on the serial display, could
   be useful in the future for limiting fan based on RPM.
   Using D882 NPN transistor for switching on / off the fans.
   The arduino is connected to a internal USB header to get the 5v and serial connection.
   The 12v is gotten from my motherboards high amperage fan header which outputs 3 amps, this is more than
   enough for the 3 case fans I'm using.
   Only 1 fan tachometer is being monitored, all the pwm lines are in parallel, they are all identical fans with
   very close pwm to rpm correlations.
*/

// Digital pin to turn on / off the fan.
const int fanControlPin = 31;
// Digital pin for the tachometer, must be a pin with a interrupt, see https://www.arduino.cc/reference/en/language/functions/external-interrupts/attachinterrupt/
const int fanTachPin = 2;
// Digital pin for the fan PWM control, must be a pin with a timer, if you change this you must change OCR4C based on the pin you picked.
const int fanPwmPin = 8;

// PWM at which the fan hits 100% RPM
const int maxPWM = 320;
// PWM at which the fan hits its minimum RPM.
const int minPWM = 48;

typedef struct {
  int temp;
  int pwm;
} pwmTemp;

// Difference in temp between the max temp and min temp, 90-55 here for example
const int tempDiff = 35;
pwmTemp pwmTempPairs[4] = {
//Temp, PWM
  {55, minPWM}, // Min temp for the fan to start spinning, how much PWM to send it
  {67, 139}, // Values are interpolated based on this and the next set
  {79, 230},
  {90, maxPWM} // Temp for which to send the max PWM to the fan
};

int fanCurve[tempDiff + 1];

// The pwm of the fan is calculated for every degree celcius and outputed to the serial interface based on above values.
void processCurve()
{
  int minTemp = pwmTempPairs[0].temp;
  float pwmDiff = (float)(maxPWM - minPWM) / tempDiff;
  for (int i = 0; i < 3; i++) {
    int tempLow = pwmTempPairs[i].temp;
    int tempHigh = pwmTempPairs[i+1].temp - (i == 2 ? 0 : 1);
    int pwmLow = pwmTempPairs[i].pwm;
    int pwmHigh = pwmTempPairs[i+1].pwm;
    float pwmOne = pwmLow;
    for (int j = tempLow - minTemp; j <= tempHigh - minTemp; j++) {
      float pwmTwo = pwmOne;
      if (pwmTwo <= pwmLow) {
        fanCurve[j] = pwmLow;
      } else if (pwmTwo >= pwmHigh) {
        fanCurve[j] = pwmHigh;
      } else {
        fanCurve[j] = round(pwmTwo);
      }
      pwmOne = pwmDiff + pwmOne;
    }
  }
  for (int i = 0; i <= tempDiff; i++) {
    if (Serial.availableForWrite() <= 1024) {
      break;
    }
    Serial.print(i);
    Serial.print(" Temp: ");
    Serial.print(i+minTemp);
    Serial.print(" Pwm: ");
    Serial.println(fanCurve[i]);
  }
}

int fanRotations;
int getRpm()
{
  int rpm = 0;
  if (digitalRead(fanControlPin) == HIGH) {
    fanRotations = 0;
    sei();
    delay (1000);
    cli();
    sei();
    rpm = fanRotations * 30;
    Serial.print (rpm, DEC);
    Serial.println("rpm");
  } else {
    delay(1000);
  }
  return rpm;
}

void rpm ()
{
  fanRotations++;
}

void setFanPWM(int value)
{
  OCR4C = value;
}

void setup()
{    
  TCCR4A = TCCR4B = TCNT4 = 0;
  ICR4   = (F_CPU/25000)/2;
  OCR4C  = ICR4/2;
  TCCR4A = _BV(COM4C1) | _BV(WGM41);
  TCCR4B = _BV(WGM43) | _BV(CS40);
  
  Serial.begin(9600);
  
  pinMode(fanTachPin, INPUT);
  attachInterrupt(digitalPinToInterrupt(fanTachPin), rpm, RISING);
  pinMode(fanPwmPin, OUTPUT);
  pinMode(fanControlPin, OUTPUT);
  processCurve();
}

void loop()
{
  getRpm();
  if (!Serial.available()) {
    return;
  }
  int cpuTemp = Serial.parseInt();
  if (!cpuTemp) {
    return;
  }
  cpuTemp = cpuTemp > pwmTempPairs[3].temp ? pwmTempPairs[3].temp : cpuTemp;
  Serial.print("Cpu Temp: ");
  Serial.println(cpuTemp);
  if (cpuTemp < pwmTempPairs[0].temp) {
    digitalWrite(fanControlPin, LOW);
    Serial.println("Fan off");
    return;
  }

  digitalWrite(fanControlPin, HIGH);
  int wRPM = fanCurve[int(floor(cpuTemp) - pwmTempPairs[0].temp)];
  setFanPWM(wRPM);
  Serial.print("Fan on at pwm: ");
  Serial.println(wRPM);
}
