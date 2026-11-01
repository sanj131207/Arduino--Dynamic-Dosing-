/* ==========================================================
   ARTIFICIAL PANCREAS SIMULATOR — TWIN-PREDICTIVE VERSION
   ----------------------------------------------------------
   What's new compared with the BASIC sketch:
   1. It watches the TREND (how fast the reading is falling),
      not just the current value.
   2. It PREDICTS where the reading will be a few seconds ahead
      (like Control-IQ's look-ahead).
   3. It sizes each dose using the digital twin's
      "base strength" (how much 1 s of pumping corrects),
      so a big meal gets a big dose and a small meal a small one.
   It still uses: L to learn normal, the saved normal (restart-
   proof), the safety limits, and prints lines the dashboard reads.
   ========================================================== */

#include <EEPROM.h>   // comes with the Arduino IDE, nothing to install

// ---------------- SETTINGS FROM THE DIGITAL TWIN ----------------
const float BASE_PER_SECOND   = 80.0;  // Twin "Base strength": acid shift removed by 1 s of pumping.
                                        // Copy the number shown on the twin page after "Learn twin".
const int   TARGET_SHIFT      = 25;    // Aim here, not 0: the twin found CO2 keeps the water ~25-30 below normal
const float LOOKAHEAD_S       = 10.0;  // How far ahead to predict (seconds)

// ---------------- WHEN TO ACT ----------------
const int   MEAL_TRIGGER      = 30;    // Act early if shift is above this AND falling fast (a meal is arriving)
const float FALL_TRIGGER      = 3.0;   // "Falling fast" = acid shift rising faster than this many counts per second
const int   CORRECTION_TRIGGER= 55;    // Act anyway if shift is above this (slow drift / leftover acid)    // Act anyway if shift is above this (slow drift / leftover acid)

// ---------------- DOSE LIMITS (SAFETY) ----------------
const unsigned long MIN_DOSE_MS = 200;   // Smallest dose the pump can give reliably
const unsigned long MAX_DOSE_MS = 3000;  // Largest single dose
const unsigned long MAX_SPIKE_MS = 6000; // Most pumping allowed for one spike, in total
const unsigned long MIX_TIME    = 8000;  // Wait after each dose (ms)
const int MAX_DOSES_IN_A_ROW    = 8;

// ---------------- YOUR HARDWARE ----------------
const bool ACID_MAKES_NUMBER_DROP = true;
const int RELAY_ON  = HIGH;
const int RELAY_OFF = LOW;
const int PH_PIN    = A0;
const int RELAY_PIN = 7;

// ---------------- SAVED MEMORY (same as the basic sketch) ----------------
const long SAVE_MARK = 12345678;
struct SavedData { long mark; int normal; };

int normalReading = -1;
int dosesInARow   = 0;
unsigned long spikeMs = 0;

// recent readings, used to measure the trend
const int HIST = 4;
unsigned long histT[HIST];
int histS[HIST];
int histN = 0;

int readSensor() {
  long total = 0;
  for (int i = 0; i < 20; i++) { total += analogRead(PH_PIN); delay(10); }
  return total / 20;
}

int acidShift(int reading) {
  return ACID_MAKES_NUMBER_DROP ? normalReading - reading : reading - normalReading;
}

void remember(int shift) {             // keep the last few readings
  if (histN < HIST) histN++;
  for (int i = histN - 1; i > 0; i--) { histT[i] = histT[i-1]; histS[i] = histS[i-1]; }
  histT[0] = millis(); histS[0] = shift;
}

float trendPerSecond() {               // how fast the acid shift is rising (counts per second)
  if (histN < 2) return 0;
  float dt = (histT[0] - histT[histN-1]) / 1000.0;
  if (dt < 0.3) return 0;
  return (histS[0] - histS[histN-1]) / dt;
}

void learnNormal() {
  Serial.println("Learning 'normal' for 10 s. Keep the probe still in PLAIN water...");
  long total = 0;
  for (int i = 0; i < 20; i++) { total += readSensor(); delay(300); }
  normalReading = total / 20;
  dosesInARow = 0; spikeMs = 0; histN = 0;
  SavedData data = { SAVE_MARK, normalReading };
  EEPROM.put(0, data);
  Serial.print("Saved normal = "); Serial.println(normalReading);
  Serial.println("---------------------------------------");
}

void checkKeyboard() {
  while (Serial.available() > 0) {
    char c = Serial.read();
    if (c == 'L' || c == 'l') learnNormal();
  }
}

void setup() {
  digitalWrite(RELAY_PIN, RELAY_OFF);
  pinMode(RELAY_PIN, OUTPUT);
  Serial.begin(9600);
  Serial.println("=== Artificial Pancreas: TWIN-PREDICTIVE mode ===");
  SavedData data; EEPROM.get(0, data);
  if (data.mark == SAVE_MARK) {
    normalReading = data.normal;
    Serial.print("STARTED - using saved normal = "); Serial.println(normalReading);
  } else {
    Serial.println("STARTED - no normal saved yet.");
  }
  Serial.println("To learn a new normal: probe settled in PLAIN water, type L, press Enter.");
  Serial.println("---------------------------------------");
}

void loop() {
  checkKeyboard();
  int reading = readSensor();

  if (normalReading < 0) {
    Serial.print("Reading: "); Serial.print(reading); Serial.println("   (waiting for you to type L)");
    delay(1000);
    return;
  }

  int shift = acidShift(reading);
  remember(shift);
  float trend = trendPerSecond();

  Serial.print("Reading: "); Serial.print(reading);
  Serial.print("   Acid shift: "); Serial.print(shift);
  Serial.print("   Trend: "); Serial.print(trend, 1); Serial.print("/s");

  bool mealArriving = (shift > MEAL_TRIGGER) && (trend > FALL_TRIGGER);
  bool tooHigh      = (shift > CORRECTION_TRIGGER);

  unsigned long doseMs = 0;
  int predicted = shift;
  if (mealArriving || tooHigh) {
    // ----- Predict, then size the dose with the twin's base strength -----
    predicted = shift + (int)(max(trend, 0.0f) * LOOKAHEAD_S);
    float needS = (predicted - TARGET_SHIFT) / BASE_PER_SECOND;
    doseMs = (unsigned long)(needS * 1000.0);
    if (doseMs < 150) doseMs = 0;                         // too small to bother
    else doseMs = constrain(doseMs, MIN_DOSE_MS, MAX_DOSE_MS);
    if (spikeMs + doseMs > MAX_SPIKE_MS) doseMs = (spikeMs < MAX_SPIKE_MS) ? MAX_SPIKE_MS - spikeMs : 0;
  }

  if (doseMs > 0) {
    dosesInARow++;
    spikeMs += doseMs;
    Serial.print("   Predicted: "); Serial.print(predicted);
    Serial.print("   -> SPIKE! Dose #"); Serial.print(dosesInARow);
    Serial.print(" ("); Serial.print(doseMs); Serial.println(" ms)");

    digitalWrite(RELAY_PIN, RELAY_ON);
    delay(doseMs);
    digitalWrite(RELAY_PIN, RELAY_OFF);

    if (dosesInARow >= MAX_DOSES_IN_A_ROW) {
      Serial.println("SAFETY STOP: too many doses. Check tubs, probe, wiring, then press RESET.");
      while (true) { }
    }
    unsigned long start = millis();
    while (millis() - start < MIX_TIME) {
      Serial.print("   mixing... reading: "); Serial.println(readSensor());
      delay(800);
    }
    histN = 0;                       // old trend no longer valid after a dose
  } else {
    if (dosesInARow > 0 && !tooHigh && !mealArriving) {
      Serial.print("   -> Back to normal after "); Serial.print(dosesInARow); Serial.println(" dose(s)");
      dosesInARow = 0; spikeMs = 0;
    } else {
      Serial.println("   -> OK");
    }
  }
  delay(500);
}
