/*
 * =============================================================================
 *  VitalScope — Sensor Calibration & Test Sketch
 * =============================================================================
 *
 *  Purpose: Test and calibrate all 3 sensors (MAX30102, MLX90614, AD8232)
 *           without needing a display. Everything runs over Serial Monitor.
 *
 *  Libraries needed:
 *    - SparkFun MAX3010x Pulse and Proximity Sensor Library
 *    - Adafruit MLX90614 Library
 *
 *  How to use:
 *    1. Flash this sketch
 *    2. Open Serial Monitor at 115200 baud
 *    3. Set Serial Monitor's "line ending" to "Newline"
 *    4. Type a number (1-9) and hit Enter to run that test
 *
 *  Menu:
 *    1 = MAX30102 continuous readings (HR + SpO2 + IR/Red raw)
 *    2 = MLX90614 continuous temp readings
 *    3 = AD8232 continuous ECG readings
 *    4 = I2C bus scan (finds all I2C devices)
 *    5 = Finger detection threshold calibration (MAX30102)
 *    6 = ECG live plotter mode (use with Arduino Serial Plotter)
 *    7 = Full sensor check (all 3 sensors at once)
 *    8 = Show pin configuration
 *    9 = Help / show menu again
 * =============================================================================
 */

#include <Wire.h>
#include "MAX30105.h"
#include "spo2_algorithm.h"
#include <Adafruit_MLX90614.h>

// ----- Pin definitions (match your wired ESP32) -----
#define I2C_SDA       23
#define I2C_SCL       19
#define ECG_OUTPUT    36    // VP — AD8232 OUTPUT
#define ECG_LO_POS    18    // AD8232 LO+
#define ECG_LO_NEG     0    // AD8232 LO-

// ----- Sensor objects -----
MAX30105 max30102;
Adafruit_MLX90614 mlx;

bool hasMax = false;
bool hasMlx = false;

// MAX30102 finger-detection threshold (starts at default, adjustable)
uint32_t fingerThreshold = 50000;

// ----- Helpers -----
void printMenu() {
  Serial.println("\n=======================================================");
  Serial.println("  VitalScope Sensor Calibration & Test");
  Serial.println("=======================================================");
  Serial.println("  Type a number (then press Enter):");
  Serial.println("");
  Serial.println("  1 = MAX30102   Heart rate + SpO2 + raw IR/Red");
  Serial.println("  2 = MLX90614   Forehead temp + ambient temp");
  Serial.println("  3 = AD8232     ECG raw values + lead-off status");
  Serial.println("  4 = I2C scan   Find all I2C devices");
  Serial.println("  5 = Finger     Calibrate MAX30102 finger threshold");
  Serial.println("  6 = ECG plot   Live ECG for Serial Plotter");
  Serial.println("  7 = All        All 3 sensors continuous readout");
  Serial.println("  8 = Pins       Show the pin configuration");
  Serial.println("  9 = Help       Show this menu again");
  Serial.println("");
  Serial.println("  Press any key during a test to stop and return here.");
  Serial.println("=======================================================");
  Serial.print("  > ");
}

void showPins() {
  Serial.println("\n--- Pin configuration ---");
  Serial.println("  I2C SDA      = GPIO 23");
  Serial.println("  I2C SCL      = GPIO 19");
  Serial.println("  ECG OUTPUT   = GPIO 36 (VP)");
  Serial.println("  ECG LO+      = GPIO 18");
  Serial.println("  ECG LO-      = GPIO 0");
  Serial.println("");
  Serial.println("  I2C devices on shared bus:");
  Serial.println("    MAX30102 @ 0x57");
  Serial.println("    MLX90614 @ 0x5A");
  Serial.println("-------------------------");
}

// Returns true if user pressed anything (so tests can exit)
bool userInterrupt() {
  if (Serial.available()) {
    while (Serial.available()) Serial.read();   // drain
    return true;
  }
  return false;
}

// ============================================================================
//  TEST 1: MAX30102 raw + computed values
// ============================================================================
void testMAX30102() {
  if (!hasMax) {
    Serial.println("ERROR: MAX30102 not found. Check wiring.");
    return;
  }

  Serial.println("\n=== MAX30102 Test ===");
  Serial.println("Place your finger on the sensor (shiny black window up).");
  Serial.println("Columns: IR, RED, FingerPresent?, HR, SpO2");
  Serial.println("(HR/SpO2 calculated once per 100 samples)");
  Serial.println("Press any key to stop.\n");

  const int BUF = 100;
  uint32_t irBuf[BUF], redBuf[BUF];
  int idx = 0;
  uint32_t lastPrint = 0;

  while (!userInterrupt()) {
    max30102.check();
    while (max30102.available() && idx < BUF) {
      redBuf[idx] = max30102.getRed();
      irBuf[idx]  = max30102.getIR();
      max30102.nextSample();
      idx++;
    }

    // Print current raw values every 200ms
    if (millis() - lastPrint > 200) {
      lastPrint = millis();
      uint32_t ir = (idx > 0) ? irBuf[idx-1] : 0;
      uint32_t red = (idx > 0) ? redBuf[idx-1] : 0;
      bool fingerOn = ir > fingerThreshold;

      Serial.printf("IR=%8u  RED=%8u  Finger=%s",
                    ir, red, fingerOn ? "YES" : "no ");

      if (idx >= BUF) {
        int32_t spo2, hr;
        int8_t vSp, vHR;
        maxim_heart_rate_and_oxygen_saturation(
            irBuf, BUF, redBuf, &spo2, &vSp, &hr, &vHR);

        Serial.printf("  |  HR=%3d (%s)  SpO2=%3d (%s)",
                      hr, vHR ? "ok" : "??",
                      spo2, vSp ? "ok" : "??");
        idx = 0;   // start over for next reading
      }
      Serial.println();
    }
  }
  Serial.println("\n--- MAX30102 test stopped ---");
}

// ============================================================================
//  TEST 2: MLX90614
// ============================================================================
void testMLX90614() {
  if (!hasMlx) {
    Serial.println("ERROR: MLX90614 not found. Check wiring.");
    return;
  }

  Serial.println("\n=== MLX90614 Test ===");
  Serial.println("Hold sensor 3-5 cm from your forehead or other target.");
  Serial.println("Columns: ObjectTemp, AmbientTemp, Difference, TargetDetected?");
  Serial.println("Press any key to stop.\n");

  uint32_t lastPrint = 0;
  while (!userInterrupt()) {
    if (millis() - lastPrint > 300) {
      lastPrint = millis();
      float obj = mlx.readObjectTempC();
      float amb = mlx.readAmbientTempC();
      float diff = obj - amb;
      bool target = (diff > 2.0f && obj > 25.0f && obj < 45.0f);

      Serial.printf("Object=%5.2f C  Ambient=%5.2f C  Diff=%+5.2f  Target=%s",
                    obj, amb, diff, target ? "YES" : "no ");

      if (target) {
        if (obj < 35.5f)       Serial.print("  [LOW]");
        else if (obj < 37.5f)  Serial.print("  [NORMAL]");
        else if (obj < 38.5f)  Serial.print("  [MILD FEVER]");
        else                   Serial.print("  [HIGH FEVER]");
      }
      Serial.println();
    }
  }
  Serial.println("\n--- MLX90614 test stopped ---");
}

// ============================================================================
//  TEST 3: AD8232
// ============================================================================
void testAD8232() {
  Serial.println("\n=== AD8232 Test ===");
  Serial.println("Attach electrodes (RA/LA/RL) before reading.");
  Serial.println("Columns: RawValue (0-4095), LO+, LO-, LeadsConnected?");
  Serial.println("Press any key to stop.\n");

  uint32_t lastPrint = 0;
  int minVal = 9999, maxVal = 0;
  int sampleCount = 0;

  while (!userInterrupt()) {
    int raw = analogRead(ECG_OUTPUT);
    if (raw < minVal) minVal = raw;
    if (raw > maxVal) maxVal = raw;
    sampleCount++;

    if (millis() - lastPrint > 300) {
      lastPrint = millis();
      int loP = digitalRead(ECG_LO_POS);
      int loN = digitalRead(ECG_LO_NEG);
      bool leads = (loP == LOW && loN == LOW);

      Serial.printf("Raw=%4d  LO+=%d  LO-=%d  Leads=%s  |  Min=%d  Max=%d  Range=%d  Samples=%d",
                    raw, loP, loN, leads ? "CONNECTED" : "OFF",
                    minVal, maxVal, maxVal - minVal, sampleCount);
      Serial.println();
    }
    delay(5);   // ~200 Hz sampling
  }
  Serial.println("\n--- AD8232 test stopped ---");
  Serial.printf("Summary: Min=%d Max=%d Range=%d over %d samples\n",
                minVal, maxVal, maxVal - minVal, sampleCount);
  Serial.println("For good ECG, Range should be at least 500-1500 when");
  Serial.println("electrodes are connected to body. Lower = noise only.");
}

// ============================================================================
//  TEST 4: I2C bus scan
// ============================================================================
void testI2CScan() {
  Serial.println("\n=== I2C Bus Scan ===");
  Serial.println("Scanning I2C addresses 0x01 to 0x7F...");

  int found = 0;
  for (byte addr = 1; addr < 127; addr++) {
    Wire.beginTransmission(addr);
    if (Wire.endTransmission() == 0) {
      Serial.printf("  Device found at 0x%02X", addr);
      if (addr == 0x57) Serial.print("  (MAX30102)");
      else if (addr == 0x5A) Serial.print("  (MLX90614)");
      else if (addr == 0x3C || addr == 0x3D) Serial.print("  (SSD1306 OLED?)");
      Serial.println();
      found++;
    }
  }
  Serial.printf("Total devices found: %d\n", found);

  if (found == 0) {
    Serial.println("\nNo devices! Check:");
    Serial.println("  - SDA wire (GPIO 23) to sensor SDA pins");
    Serial.println("  - SCL wire (GPIO 19) to sensor SCL pins");
    Serial.println("  - 3.3V power to sensors");
    Serial.println("  - GND to sensors");
  }
}

// ============================================================================
//  TEST 5: Finger threshold calibration
// ============================================================================
void calibrateFingerThreshold() {
  if (!hasMax) { Serial.println("MAX30102 not found."); return; }

  Serial.println("\n=== Finger Threshold Calibration ===");
  Serial.println("Step 1: REMOVE your finger. Reading for 5 seconds...");
  delay(1000);

  uint32_t maxNoFinger = 0;
  uint32_t start = millis();
  while (millis() - start < 5000) {
    max30102.check();
    if (max30102.available()) {
      uint32_t ir = max30102.getIR();
      if (ir > maxNoFinger) maxNoFinger = ir;
      max30102.nextSample();
    }
  }
  Serial.printf("  Max IR with NO finger: %u\n", maxNoFinger);

  Serial.println("\nStep 2: PLACE your finger on the sensor. Reading for 5 seconds...");
  Serial.println("  (Count down: 3... 2... 1...)");
  delay(3000);

  uint32_t minWithFinger = 4000000000UL;
  start = millis();
  while (millis() - start < 5000) {
    max30102.check();
    if (max30102.available()) {
      uint32_t ir = max30102.getIR();
      if (ir < minWithFinger) minWithFinger = ir;
      max30102.nextSample();
    }
  }
  Serial.printf("  Min IR with finger:    %u\n", minWithFinger);

  if (minWithFinger > maxNoFinger) {
    uint32_t threshold = (maxNoFinger + minWithFinger) / 2;
    fingerThreshold = threshold;
    Serial.printf("\n  Recommended threshold: %u\n", threshold);
    Serial.printf("  Put this value in your main code: fingerThreshold = %u;\n", threshold);
  } else {
    Serial.println("\n  ERROR: Could not distinguish finger from no-finger.");
    Serial.println("  Possible causes:");
    Serial.println("   - Finger is in front of sensor during 'remove' step");
    Serial.println("   - Ambient light leaking into sensor");
    Serial.println("   - Sensor LEDs weak");
  }
}

// ============================================================================
//  TEST 6: ECG Plotter (for Arduino Serial Plotter)
// ============================================================================
void ecgPlotterMode() {
  Serial.println("\n=== ECG Plotter Mode ===");
  Serial.println("Close Serial Monitor.");
  Serial.println("Open Tools -> Serial Plotter (Ctrl+Shift+L).");
  Serial.println("You should see your ECG waveform.");
  Serial.println("Press any key on keyboard to stop (or close plotter).");
  delay(3000);

  while (!userInterrupt()) {
    int raw = analogRead(ECG_OUTPUT);
    int loP = digitalRead(ECG_LO_POS) * 2000;  // scaled so you can see it
    int loN = digitalRead(ECG_LO_NEG) * 2000;
    // Print 3 series: ECG, LO+, LO-
    Serial.print(raw); Serial.print(",");
    Serial.print(loP); Serial.print(",");
    Serial.println(loN);
    delay(5);
  }
}

// ============================================================================
//  TEST 7: All sensors
// ============================================================================
void testAllSensors() {
  Serial.println("\n=== All Sensors — Continuous Readout ===");
  Serial.println("Reading all 3 sensors at once.");
  Serial.println("Press any key to stop.\n");

  uint32_t lastPrint = 0;
  while (!userInterrupt()) {
    if (millis() - lastPrint > 500) {
      lastPrint = millis();

      // MAX30102
      uint32_t ir = 0, red = 0;
      if (hasMax) {
        max30102.check();
        if (max30102.available()) {
          ir = max30102.getIR();
          red = max30102.getRed();
          max30102.nextSample();
        }
      }

      // MLX90614
      float obj = hasMlx ? mlx.readObjectTempC() : -99.0f;
      float amb = hasMlx ? mlx.readAmbientTempC() : -99.0f;

      // AD8232
      int ecg = analogRead(ECG_OUTPUT);
      int loP = digitalRead(ECG_LO_POS);
      int loN = digitalRead(ECG_LO_NEG);

      Serial.printf("MAX: IR=%7u R=%7u   |   MLX: obj=%5.2f amb=%5.2f   |   ECG: raw=%4d LO+=%d LO-=%d\n",
                    ir, red, obj, amb, ecg, loP, loN);
    }
  }
  Serial.println("\n--- Stopped ---");
}

// ============================================================================
//  SETUP
// ============================================================================
void setup() {
  Serial.begin(115200);
  delay(1000);

  Serial.println("\n\n===============================================");
  Serial.println(" VitalScope Sensor Calibration");
  Serial.println("===============================================");

  // --- I2C ---
  Serial.println("Starting I2C bus...");
  Wire.begin(I2C_SDA, I2C_SCL, 400000);
  delay(200);

  // --- MAX30102 ---
  Serial.print("Initializing MAX30102... ");
  if (max30102.begin(Wire, I2C_SPEED_FAST)) {
    max30102.setup(60, 4, 2, 100, 411, 4096);
    hasMax = true;
    Serial.println("OK");
  } else {
    Serial.println("NOT FOUND");
  }

  // --- MLX90614 ---
  Serial.print("Initializing MLX90614... ");
  if (mlx.begin()) {
    hasMlx = true;
    Serial.println("OK");
  } else {
    Serial.println("NOT FOUND");
  }

  // --- AD8232 ---
  Serial.print("Initializing AD8232... ");
  pinMode(ECG_LO_POS, INPUT);
  pinMode(ECG_LO_NEG, INPUT);
  analogReadResolution(12);
  analogSetPinAttenuation(ECG_OUTPUT, ADC_11db);
  Serial.println("OK (analog)");

  Serial.println("");
  Serial.printf("Status: MAX30102=%s  MLX90614=%s  AD8232=%s\n",
                hasMax ? "OK" : "MISSING",
                hasMlx ? "OK" : "MISSING",
                "OK");

  printMenu();
}

// ============================================================================
//  LOOP
// ============================================================================
void loop() {
  if (!Serial.available()) return;

  int c = Serial.read();
  // Drain any remaining input
  while (Serial.available()) Serial.read();

  switch (c) {
    case '1': testMAX30102();             break;
    case '2': testMLX90614();             break;
    case '3': testAD8232();               break;
    case '4': testI2CScan();              break;
    case '5': calibrateFingerThreshold(); break;
    case '6': ecgPlotterMode();           break;
    case '7': testAllSensors();           break;
    case '8': showPins();                 break;
    case '9': printMenu();                break;
    case '\n': case '\r': return;         // ignore newlines silently
    default:
      Serial.printf("Unknown command '%c'. Type 9 for menu.\n", (char)c);
  }
  printMenu();
}
