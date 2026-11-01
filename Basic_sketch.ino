/* ==========================================================
   ARTIFICIAL PANCREAS SIMULATOR — BASIC VERSION
   (fixed dose, restart-proof)

   1. You tell it what "normal" is: probe in plain water,
      type L in the Serial Monitor and press Enter
   2. It keeps checking the pH sensor
   3. If the water turns acidic (simulated glucose spike),
      it runs the pump for a FIXED time (simulated insulin dose)
   4. Waits for the liquid to mix, then checks again

   "Normal" is saved in the Arduino's permanent memory, so a
   restart (or reopening the Serial Monitor) does NOT make it
   re-learn from acidic water.
   ========================================================== */

#include <EEPROM.h>   // comes with the Arduino IDE, nothing to install

// ---------------- SETTINGS YOU CAN CHANGE ----------------
const int ALARM_LEVEL = 50;               // How far the reading must move toward acid before dosing.
                                          // Your noise is about ±5 and lemon moved it ~230, so 50 sits safely between.
const unsigned long DOSE_TIME = 1000;     // Pump run time per dose in milliseconds (1000 = 1 second)
const unsigned long MIX_TIME  = 15000;    // Wait after each dose so the liquid mixes and the probe catches up
const int MAX_DOSES_IN_A_ROW  = 8;        // Safety limit: stop if this many doses in a row don't fix it
const bool ACID_MAKES_NUMBER_DROP = true; // Your test: lemon took it from ~458 down to ~228
const int RELAY_ON  = HIGH;               // Your relay switches ON with HIGH
const int RELAY_OFF = LOW;

// ---------------- PINS ----------------
const int PH_PIN    = A0;   // pH board analog OUTs
const int RELAY_PIN = 7;    // relay IN

// ---------------- SAVED MEMORY ----------------
const long SAVE_MARK = 12345678;   // a code meaning "a normal value has been saved"
struct SavedData {
  long mark;
  int  normal;
};

int normalReading = -1;   // -1 means "not learned yet"
int dosesInARow   = 0;    // counts doses for the current spike

// Take 20 quick readings and average them (smooths out jitter)
int readSensor() {
  long total = 0;
  for (int i = 0; i < 20; i++) {
    total += analogRead(PH_PIN);
    delay(10);
  }
  return total / 20;
}

// How far the water has moved toward "acid" (positive = more acidic than normal)
int acidShift(int reading) {
  if (ACID_MAKES_NUMBER_DROP) {
    return normalReading - reading;
  } else {
    return reading - normalReading;
  }
}

// Learn "normal" from the current water and save it permanently
void learnNormal() {
  Serial.println("Learning 'normal' for 10 s. Keep the probe still in PLAIN water...");
  long total = 0;
  for (int i = 0; i < 20; i++) {
    total += readSensor();
    delay(300);
  }
  normalReading = total / 20;
  dosesInARow = 0;

  SavedData data = { SAVE_MARK, normalReading };
  EEPROM.put(0, data);

  Serial.print("Saved normal = ");
  Serial.println(normalReading);
  Serial.println("---------------------------------------");
}

// If you typed L (and pressed Enter) in the Serial Monitor, re-learn normal
void checkKeyboard() {
  while (Serial.available() > 0) {
    char c = Serial.read();
    if (c == 'L' || c == 'l') {
      learnNormal();
    }
  }
}

void setup() {
  digitalWrite(RELAY_PIN, RELAY_OFF);  // set the OFF level first...
  pinMode(RELAY_PIN, OUTPUT);          // ...then make it an output (avoids a start-up twitch)
  Serial.begin(9600);

  Serial.println("=== Artificial Pancreas: BASIC mode ===");

  // Look for a saved "normal" from before
  SavedData data;
  EEPROM.get(0, data);
  if (data.mark == SAVE_MARK) {
    normalReading = data.normal;
    Serial.print("STARTED - using saved normal = ");
    Serial.println(normalReading);
  } else {
    Serial.println("STARTED - no normal saved yet.");
  }
  Serial.println("To learn a new normal: probe settled in PLAIN water, type L, press Enter.");
  Serial.println("---------------------------------------");
}

void loop() {
  checkKeyboard();

  int reading = readSensor();

  // Nothing learned yet: just show readings, never dose
  if (normalReading < 0) {
    Serial.print("Reading: ");
    Serial.print(reading);
    Serial.println("   (waiting for you to type L)");
    delay(1000);
    return;
  }

  int shift = acidShift(reading);

  Serial.print("Reading: ");
  Serial.print(reading);
  Serial.print("   Acid shift: ");
  Serial.print(shift);

  if (shift > ALARM_LEVEL) {
    // ----- Spike detected: give one fixed insulin dose -----
    dosesInARow++;
    Serial.print("   -> SPIKE! Dose #");
    Serial.println(dosesInARow);

    digitalWrite(RELAY_PIN, RELAY_ON);
    delay(DOSE_TIME);
    digitalWrite(RELAY_PIN, RELAY_OFF);

    // ----- Safety limit -----
    if (dosesInARow >= MAX_DOSES_IN_A_ROW) {
      Serial.println("SAFETY STOP: too many doses. Check tubs, probe, wiring, then press RESET.");
      while (true) { }   // freeze here with the pump OFF
    }

    // ----- Let it mix, showing readings as it recovers -----
    unsigned long start = millis();
    while (millis() - start < MIX_TIME) {
      Serial.print("   mixing... reading: ");
      Serial.println(readSensor());
      delay(800);
    }
  } else {
    if (dosesInARow > 0) {
      Serial.print("   -> Back to normal after ");
      Serial.print(dosesInARow);
      Serial.println(" dose(s)");
    } else {
      Serial.println("   -> OK");
    }
    dosesInARow = 0;
  }

  delay(500);
}