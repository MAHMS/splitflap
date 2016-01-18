/*
   Copyright 2015 Scott Bezek

   Licensed under the Apache License, Version 2.0 (the "License");
   you may not use this file except in compliance with the License.
   You may obtain a copy of the License at

       http://www.apache.org/licenses/LICENSE-2.0

   Unless required by applicable law or agreed to in writing, software
   distributed under the License is distributed on an "AS IS" BASIS,
   WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
   See the License for the specific language governing permissions and
   limitations under the License.
*/

#include <math.h>

#define NUM_FLAPS (40)

#define STEPS_PER_REVOLUTION (4096.0)
//(63.68395 * 64)
#define STEPS_PER_FLAP (STEPS_PER_REVOLUTION / NUM_FLAPS)

const int flaps[NUM_FLAPS] = {
  ' ',
  'a', 'b', 'c', 'd', 'e', 'f', 'g', 'h', 'i', 'j', 'k', 'l', 'm',
  'n', 'o', 'p', 'q', 'r', 's', 't', 'u', 'v', 'w', 'x', 'y', 'z',
  '0', '1', '2', '3', '4', '5', '6', '7', '8', '9',
  '.',
  ',',
  '\'',
};

uint8_t step_pattern[] = {
  B1001,
  B1001,
  B1100,
  B1100,
  B0110,
  B0110,
  B0011,
  B0011,
};

bool initializing = false;
long initStartMicros;

unsigned long lastUpdate = 0;
long current = 0;
float desired = 0;

#define MAX_PERIOD_MICROS (10000)
#define MIN_PERIOD_MICROS (800)

#define ACCEL_TIME_MICROS (200000)
#define MAX_RAMP_LEVELS (ACCEL_TIME_MICROS/MIN_PERIOD_MICROS)

int RAMP_PERIODS[MAX_RAMP_LEVELS+2];

int computedMaxRampLevel;

int curRampLevel = 0;

long stepPeriod = 0;

int lastHome = LOW;

int desiredFlapIndex = 0;

int findFlapIndex(int character) {
  for (int i = 0; i < NUM_FLAPS; i++) {
    if (character == flaps[i]) {
      return i;
    }
  }
  return -1;
}

void setup() {
  // put your setup code here, to run once:
  DDRD = 0xF;
  pinMode(13, OUTPUT);
  Serial.begin(115200);

  pinMode(31, INPUT);
  digitalWrite(31, HIGH);
  delay(5);
  lastHome = digitalRead(31);

  while(!Serial) {}
  computeAccelerationRamp();
}

void computeAccelerationRamp() {
  float minVel = 1000000. / MAX_PERIOD_MICROS;
  float maxVel = 1000000. / MIN_PERIOD_MICROS;
  long t = 0;
  int i = 1;
  RAMP_PERIODS[0] = 0;
  Serial.print("Computing acceleration table: \n[\n");
  while (t < ACCEL_TIME_MICROS) {
    float vel = minVel + (maxVel - minVel) * ((float)t/ACCEL_TIME_MICROS);
    if (vel > maxVel) {
      vel = maxVel;
    }
    int period = (int)(1000000. / vel);

    Serial.print(period);
    Serial.print(",\n");

    RAMP_PERIODS[i] = period;
    t += period;
    i++;

    if (i >= MAX_RAMP_LEVELS + 1) {
      panic();
    }
  }
  computedMaxRampLevel = i - 1;
  Serial.print("]\n\nVelocity steps: ");
  Serial.print(computedMaxRampLevel);
  Serial.print("\n\n");

//  goHome();
//  delay(200);
  initializing = true;
  initStartMicros = micros();
  desired = STEPS_PER_REVOLUTION;
}

void panic() {
  PORTD = 0;
  pinMode(13, OUTPUT);
  while (1) {
    digitalWrite(13, HIGH);
    delay(100);
    digitalWrite(13, LOW);
    delay(100);
  }
}

void goHome() {
  boolean foundHome = false;

  for (int i = 0; i < STEPS_PER_REVOLUTION + STEPS_PER_FLAP * 5; i++) {
    int curHome = digitalRead(31);
    bool shift = curHome == HIGH && lastHome == LOW;
    lastHome = curHome;

    if (shift) {
      foundHome = true;
      break;
    }

    PORTD = step_pattern[i & B111];
    delayMicroseconds(RAMP_PERIODS[computedMaxRampLevel/6]);
  }
  PORTD = 0;

  if (!foundHome) {
    panic();
  }
}

void loop() {
  unsigned long now = micros();
  if (now - lastUpdate >= stepPeriod) {
    if (curRampLevel > 0) {
      current++;
    } else if (curRampLevel < 0) {
      current--;
    }
    lastUpdate = now;

    if (Serial.available() > 0) {
      int b = Serial.read();
      switch (b) {
        case '{':
          desired += STEPS_PER_FLAP / 4;
          break;
        case '[':
          desired += STEPS_PER_FLAP;
          break;
        case ']':
          desired -= STEPS_PER_FLAP;
          break;
        case '}':
          desired -= STEPS_PER_FLAP / 4;
          break;
        case '!':
          if (PORTD == 0) {
            desiredFlapIndex = 0;
          }
          break;
        case '@':
          goHome();
          desiredFlapIndex = 0;
          current = (int)desired;
          break;
        default:
          int flapIndex = findFlapIndex(b);
          if (flapIndex != -1) {
            int deltaFlaps = flapIndex - desiredFlapIndex;

            // Always only travel in the forward direction
            if (deltaFlaps <= 0) {
              deltaFlaps += NUM_FLAPS;
            }
            desiredFlapIndex = (desiredFlapIndex + deltaFlaps) % NUM_FLAPS;
            desired += STEPS_PER_FLAP * deltaFlaps;
          }
          break;
      }
    }
      
    float delta = desired - current;
    if (delta > -1 && delta < 1) {
      curRampLevel = 0;
      stepPeriod = 0;
      PORTD = 0;

      if (initializing) {
        initializing = false;
        Serial.print((micros() - initStartMicros));
        Serial.print(" us per revolution\n\n"); 
      }

      while (current > 64) {
        current -= 64;
        desired -= 64;
      }
      while (current < -64) {
        current += 64;
        desired += 64;
      }
      return;
    }

    if (PORTD == 0) {
      PORTD = step_pattern[current & B111];
      delay(10);
      PORTD = step_pattern[(current-1) & B111];
      delay(10);
      PORTD = step_pattern[(current-2) & B111];
      delay(10);
      PORTD = step_pattern[(current-3) & B111];
      delay(10);
      PORTD = step_pattern[(current-2) & B111];
      delay(10);
      PORTD = step_pattern[(current-1) & B111];
      delay(10);
      PORTD = step_pattern[current & B111];
      delay(10);
    }

    PORTD = step_pattern[current & B111];

    int desiredRampLevel = 0;
    if (delta > computedMaxRampLevel) {
      desiredRampLevel = computedMaxRampLevel;
    } else if (delta < -computedMaxRampLevel) {
      desiredRampLevel = -computedMaxRampLevel;
    } else {
      desiredRampLevel = (int)delta;
    }
    if (curRampLevel > desiredRampLevel) {
      curRampLevel--;
    } else if (curRampLevel < desiredRampLevel) {
      curRampLevel++;
    }

    stepPeriod = curRampLevel > 0 ? RAMP_PERIODS[curRampLevel] : RAMP_PERIODS[-curRampLevel];
  }
}
