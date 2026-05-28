/*
 * =============================================================================
 *  VitalScope — OLED Edition with DS18B20
 *  ESP32 + 0.96" SSD1306 OLED + MAX30102 + DS18B20 + AD8232 + 3 push buttons
 * =============================================================================
 *
 *  Libraries needed (Library Manager):
 *    - Adafruit GFX Library
 *    - Adafruit SSD1306
 *    - SparkFun MAX3010x Pulse and Proximity Sensor Library
 *    - OneWire (by Paul Stoffregen)
 *    - DallasTemperature (by Miles Burton)
 *
 *  Wiring:
 *    OLED:     VCC=3.3V GND=GND SCL=GPIO19 SDA=GPIO23
 *    MAX30102: VIN=3.3V GND=GND SCL=GPIO19 SDA=GPIO23 (same bus as OLED)
 *    DS18B20:  RED=3.3V BLACK=GND YELLOW=GPIO26 (+4.7kΩ pull-up to 3.3V!)
 *    AD8232:   3.3V/GND  OUTPUT=GPIO36  LO+=GPIO18  LO-=GPIO0
 *    Buttons:  UP=GPIO32  DOWN=GPIO33  SELECT=GPIO25  (other side to GND)
 * =============================================================================
 */

#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include "MAX30105.h"
#include "spo2_algorithm.h"
#include <OneWire.h>
#include <DallasTemperature.h>
#include <math.h>

// ----- Pins -----
#define I2C_SDA       23
#define I2C_SCL       19

#define ECG_OUTPUT    36
#define ECG_LO_POS    18
#define ECG_LO_NEG     0

#define DS18B20_PIN   26

#define BTN_UP        32
#define BTN_DOWN      33
#define BTN_SELECT    25

// ----- OLED -----
#define OLED_W        128
#define OLED_H         64
#define OLED_ADDR    0x3C
#define OLED_RESET   -1

Adafruit_SSD1306 oled(OLED_W, OLED_H, &Wire, OLED_RESET);

// ----- Sensors -----
MAX30105 max30102;
OneWire oneWire(DS18B20_PIN);
DallasTemperature ds(&oneWire);

bool hasMax = false;
bool hasDs  = false;
float dsBaseline = 25.0f;

// MAX30102 buffer
const int MAX_BUF = 100;
uint32_t irBuf[MAX_BUF];
uint32_t redBuf[MAX_BUF];
int bufIdx = 0;

// ----- App state -----
enum ScreenId {
  SCR_BOOT, SCR_HOME, SCR_HR, SCR_TEMP, SCR_ECG, SCR_ABOUT
};
ScreenId currentScreen = SCR_BOOT;
bool screenEntered = true;

int menuSelected = 0;
const int NUM_MENU_ITEMS = 4;
const char* MENU_ITEMS[] = {
  "Heart Rate / SpO2",
  "Body Temperature",
  "ECG Waveform",
  "About"
};

// ----- Button handling -----
struct Button {
  int pin;
  bool lastState;
  uint32_t lastChange;
  bool justPressed;
};

Button btnUp     = {BTN_UP,     HIGH, 0, false};
Button btnDown   = {BTN_DOWN,   HIGH, 0, false};
Button btnSelect = {BTN_SELECT, HIGH, 0, false};

void updateButton(Button& b) {
  bool s = digitalRead(b.pin);
  b.justPressed = false;
  if (s != b.lastState && millis() - b.lastChange > 30) {
    b.lastChange = millis();
    if (s == LOW && b.lastState == HIGH) b.justPressed = true;
    b.lastState = s;
  }
}

void updateButtons() {
  updateButton(btnUp);
  updateButton(btnDown);
  updateButton(btnSelect);
}

// ----- Helpers -----
float phase01(uint32_t periodMs) {
  return (float)(millis() % periodMs) / (float)periodMs;
}

void drawCenteredText(const char* txt, int cy, uint8_t size = 1) {
  oled.setTextSize(size);
  int w = strlen(txt) * 6 * size;
  oled.setCursor((OLED_W - w) / 2, cy);
  oled.print(txt);
}

// ----- Sensor wrappers -----
void sensorsInit() {
  Wire.begin(I2C_SDA, I2C_SCL, 400000);
  delay(100);

  if (max30102.begin(Wire, I2C_SPEED_FAST)) {
    max30102.setup(60, 4, 2, 100, 411, 4096);
    hasMax = true;
    Serial.println("[MAX30102] OK");
  } else {
    Serial.println("[MAX30102] not found");
  }

  ds.begin();
  ds.setResolution(11);
  hasDs = (ds.getDeviceCount() > 0);
  if (hasDs) {
    ds.requestTemperatures();
    delay(400);
    float c = ds.getTempCByIndex(0);
    if (c != DEVICE_DISCONNECTED_C) dsBaseline = c;
    Serial.printf("[DS18B20] OK, baseline=%.2f\n", dsBaseline);
  } else {
    Serial.println("[DS18B20] not found");
  }

  pinMode(ECG_LO_POS, INPUT);
  pinMode(ECG_LO_NEG, INPUT);
  analogReadResolution(12);
  analogSetPinAttenuation(ECG_OUTPUT, ADC_11db);
}

void maxReset() { bufIdx = 0; }

bool maxFingerPresent() {
  if (!hasMax) return false;
  max30102.check();
  if (max30102.available()) {
    uint32_t ir = max30102.getIR();
    max30102.nextSample();
    return ir > 50000;
  }
  return false;
}

bool maxPollOne() {
  if (!hasMax) return false;
  max30102.check();
  while (max30102.available() && bufIdx < MAX_BUF) {
    redBuf[bufIdx] = max30102.getRed();
    irBuf[bufIdx]  = max30102.getIR();
    max30102.nextSample();
    bufIdx++;
  }
  return bufIdx >= MAX_BUF;
}

float maxProgress() { return (float)bufIdx / (float)MAX_BUF; }

bool maxCompute(int32_t& hr, int32_t& spo2) {
  if (bufIdx < MAX_BUF) return false;
  int8_t vHR, vSp;
  maxim_heart_rate_and_oxygen_saturation(
      irBuf, MAX_BUF, redBuf, &spo2, &vSp, &hr, &vHR);
  return vHR && vSp && hr > 30 && hr < 220 && spo2 > 70 && spo2 <= 100;
}

void dsRequest()       { if (hasDs) ds.requestTemperatures(); }
float dsRead()         { return hasDs ? ds.getTempCByIndex(0) : NAN; }
bool  dsProbeTouched() {
  if (!hasDs) return false;
  float c = ds.getTempCByIndex(0);
  if (c == DEVICE_DISCONNECTED_C) return false;
  return c > dsBaseline + 1.5f;
}
void dsCalibrateBaseline() {
  if (!hasDs) return;
  ds.requestTemperatures();
  delay(400);
  float c = ds.getTempCByIndex(0);
  if (c != DEVICE_DISCONNECTED_C) dsBaseline = c;
}

int ecgRaw() { return analogRead(ECG_OUTPUT); }
bool ecgLeadsConnected() {
  return digitalRead(ECG_LO_POS) == LOW && digitalRead(ECG_LO_NEG) == LOW;
}

// =============================================================================
//  ANIMATIONS — adapted for 128x64 monochrome
// =============================================================================
void drawFingerAnim(int cx, int cy, float phase) {
  float s = 1.0f + 0.15f * sinf(phase * 2.0f * PI);
  int rX = (int)(8 * s);
  int rY = (int)(14 * s);
  oled.fillRoundRect(cx - rX, cy - rY, rX * 2, rY * 2, rX, SSD1306_WHITE);
  oled.fillRoundRect(cx - rX + 2, cy - rY + 2, rX * 2 - 4, 6, 3, SSD1306_BLACK);
}

void drawHeartFill(int cx, int cy, float fill) {
  fill = constrain(fill, 0.0f, 1.0f);
  int size = 22;
  int r = size / 2;
  int topY = cy - size / 2;

  oled.drawCircle(cx - r / 2, topY + r / 2, r / 2, SSD1306_WHITE);
  oled.drawCircle(cx + r / 2, topY + r / 2, r / 2, SSD1306_WHITE);
  oled.drawLine(cx - r, topY + r / 2, cx, cy + r, SSD1306_WHITE);
  oled.drawLine(cx + r, topY + r / 2, cx, cy + r, SSD1306_WHITE);

  int fillTopY = cy + r - (int)(fill * size);
  for (int y = fillTopY; y <= cy + r; y++) {
    int dy = y - topY;
    int halfW;
    if (dy < r) {
      int cy1 = topY + r / 2;
      int d = abs(y - cy1);
      if (d > r / 2) continue;
      halfW = r / 2 + (int)sqrtf((float)(r * r / 4 - d * d));
    } else {
      halfW = (int)((float)r * (1.0f - (float)(dy - r) / r));
    }
    oled.drawFastHLine(cx - halfW, y, halfW * 2, SSD1306_WHITE);
  }
}

// Animated thermometer — bulb fills as time passes
void drawThermometerAnim(int cx, int cy, float phase) {
  // Body of thermometer
  oled.drawRoundRect(cx - 3, cy - 18, 7, 28, 3, SSD1306_WHITE);
  // Bulb
  oled.fillCircle(cx, cy + 12, 5, SSD1306_WHITE);
  // Animated mercury column (rises and falls)
  float s = 0.5f + 0.5f * sinf(phase * 2.0f * PI);
  int barH = (int)(s * 22);
  oled.fillRect(cx - 1, cy + 8 - barH, 4, barH, SSD1306_WHITE);
  // Tick marks
  for (int t = 0; t < 4; t++) {
    oled.drawFastHLine(cx + 5, cy - 14 + t * 7, 3, SSD1306_WHITE);
  }
}

void drawEcgElectrodes(int cx, int cy, float phase) {
  float s = 0.5f + 0.5f * sinf(phase * 2.0f * PI);
  int rOuter = 4 + (int)(s * 4);
  int pts[3][2] = { {cx - 25, cy - 8}, {cx + 25, cy - 8}, {cx, cy + 12} };
  const char* lbl[3] = { "RA", "LA", "RL" };
  for (int i = 0; i < 3; i++) {
    oled.drawCircle(pts[i][0], pts[i][1], rOuter, SSD1306_WHITE);
    oled.fillCircle(pts[i][0], pts[i][1], 3, SSD1306_WHITE);
    oled.setTextSize(1);
    oled.setCursor(pts[i][0] - 5, pts[i][1] + 8);
    oled.print(lbl[i]);
  }
}

void drawSpinner(int cx, int cy, int r, float phase) {
  float startA = phase * 2.0f * PI;
  float span = PI * 1.4f;
  const int steps = 20;
  for (int i = 0; i < steps; i++) {
    float a = startA + span * ((float)i / steps);
    int x = cx + (int)(cosf(a) * r);
    int y = cy + (int)(sinf(a) * r);
    oled.drawPixel(x, y, SSD1306_WHITE);
    oled.drawPixel(x + 1, y, SSD1306_WHITE);
  }
}

void drawProgressBar(int x, int y, int w, int h, float p) {
  p = constrain(p, 0.0f, 1.0f);
  oled.drawRoundRect(x, y, w, h, h / 2, SSD1306_WHITE);
  int fillW = (int)((w - 4) * p);
  if (fillW > 0) oled.fillRoundRect(x + 2, y + 2, fillW, h - 4, (h - 4) / 2, SSD1306_WHITE);
}

// =============================================================================
//  BOOT SPLASH
// =============================================================================
void runBootSplash() {
  uint32_t start = millis();
  const uint32_t duration = 2500;
  const char* steps[] = { "Init display...", "Init I2C bus...",
                          "Detect sensors...", "Ready" };

  while (millis() - start < duration) {
    oled.clearDisplay();
    int cx = OLED_W / 2;
    int cy = 16;
    oled.fillCircle(cx - 4, cy - 1, 5, SSD1306_WHITE);
    oled.fillCircle(cx + 4, cy - 1, 5, SSD1306_WHITE);
    oled.fillTriangle(cx - 9, cy + 2, cx + 9, cy + 2, cx, cy + 12, SSD1306_WHITE);
    drawSpinner(cx, cy + 5, 18, phase01(1200));

    oled.setTextSize(1);
    oled.setCursor((OLED_W - 11 * 6) / 2, 38);
    oled.print("VitalScope");

    int idx = (millis() - start) / (duration / 4);
    if (idx > 3) idx = 3;
    int slen = strlen(steps[idx]);
    oled.setCursor((OLED_W - slen * 6) / 2, 52);
    oled.print(steps[idx]);

    oled.display();
    delay(40);
  }
}

// =============================================================================
//  HOME / MENU
// =============================================================================
void drawHome() {
  oled.clearDisplay();
  oled.setTextSize(1);
  oled.setCursor(0, 0);
  oled.print("Health Monitor");
  oled.drawFastHLine(0, 10, OLED_W, SSD1306_WHITE);

  // Show 3 items at a time, scrolling around the selected one
  int firstItem = 0;
  if (menuSelected >= 3) firstItem = menuSelected - 2;

  for (int i = 0; i < 3 && firstItem + i < NUM_MENU_ITEMS; i++) {
    int actualIdx = firstItem + i;
    int y = 14 + i * 16;
    if (actualIdx == menuSelected) {
      oled.fillRoundRect(2, y - 2, OLED_W - 4, 14, 2, SSD1306_WHITE);
      oled.setTextColor(SSD1306_BLACK);
    } else {
      oled.setTextColor(SSD1306_WHITE);
    }
    oled.setCursor(8, y);
    oled.print(MENU_ITEMS[actualIdx]);
  }
  oled.setTextColor(SSD1306_WHITE);

  // Scroll indicator on the right
  if (NUM_MENU_ITEMS > 3) {
    int barH = 30 / NUM_MENU_ITEMS * 3;
    int barY = 14 + (menuSelected * (30 - barH)) / (NUM_MENU_ITEMS - 1);
    oled.fillRect(OLED_W - 2, barY, 2, barH, SSD1306_WHITE);
  }

  oled.display();
}

void runHome() {
  if (screenEntered) {
    drawHome();
    screenEntered = false;
  }

  bool needsRedraw = false;

  if (btnUp.justPressed) {
    menuSelected = (menuSelected - 1 + NUM_MENU_ITEMS) % NUM_MENU_ITEMS;
    needsRedraw = true;
  }
  if (btnDown.justPressed) {
    menuSelected = (menuSelected + 1) % NUM_MENU_ITEMS;
    needsRedraw = true;
  }
  if (btnSelect.justPressed) {
    if      (menuSelected == 0) currentScreen = SCR_HR;
    else if (menuSelected == 1) currentScreen = SCR_TEMP;
    else if (menuSelected == 2) currentScreen = SCR_ECG;
    else if (menuSelected == 3) currentScreen = SCR_ABOUT;
    screenEntered = true;
    return;
  }

  if (needsRedraw) drawHome();
  delay(20);
}

// =============================================================================
//  HR / SpO2
// =============================================================================
enum HRState { HR_WAIT, HR_MEASURE, HR_DONE };
HRState hrState;
int32_t hrBpm, hrSpo2;
bool hrValid;

void hrEnter() {
  hrState = HR_WAIT;
  maxReset();
}

void runHR() {
  if (screenEntered) { hrEnter(); screenEntered = false; }

  if (btnUp.justPressed || (btnSelect.justPressed && hrState == HR_DONE)) {
    if (hrState == HR_DONE && btnSelect.justPressed) {
      hrEnter();
      return;
    }
    currentScreen = SCR_HOME;
    screenEntered = true;
    return;
  }

  oled.clearDisplay();
  oled.setTextSize(1);
  oled.setTextColor(SSD1306_WHITE);

  switch (hrState) {
    case HR_WAIT: {
      oled.setCursor(0, 0);
      oled.print("HR & SpO2");
      oled.drawFastHLine(0, 10, OLED_W, SSD1306_WHITE);
      drawFingerAnim(OLED_W / 2, 32, phase01(1200));
      drawCenteredText("Place finger", 50);
      drawCenteredText("on sensor", 58);

      if (maxFingerPresent()) {
        hrState = HR_MEASURE;
        maxReset();
      }
      break;
    }
    case HR_MEASURE: {
      oled.setCursor(0, 0);
      oled.print("Measuring...");
      oled.drawFastHLine(0, 10, OLED_W, SSD1306_WHITE);

      bool full = maxPollOne();
      float p = maxProgress();
      drawHeartFill(OLED_W / 2, 28, p);
      drawProgressBar(14, 48, 100, 6, p);

      char pct[16];
      snprintf(pct, sizeof(pct), "%d%%", (int)(p * 100));
      drawCenteredText(pct, 56);

      if (!maxFingerPresent() && p < 0.5f) {
        hrState = HR_WAIT;
        break;
      }
      if (full) {
        hrValid = maxCompute(hrBpm, hrSpo2);
        hrState = HR_DONE;
      }
      break;
    }
    case HR_DONE: {
      oled.setCursor(0, 0);
      oled.print("Results");
      oled.drawFastHLine(0, 10, OLED_W, SSD1306_WHITE);

      oled.setTextSize(1);
      oled.setCursor(8, 16);
      oled.print("HR:");
      oled.setTextSize(2);
      oled.setCursor(8, 26);
      oled.print(hrValid ? String(hrBpm) : "--");
      oled.setTextSize(1);
      oled.setCursor(8, 48);
      oled.print("BPM");

      oled.setTextSize(1);
      oled.setCursor(72, 16);
      oled.print("SpO2:");
      oled.setTextSize(2);
      oled.setCursor(72, 26);
      oled.print(hrValid ? String(hrSpo2) : "--");
      oled.setTextSize(1);
      oled.setCursor(72, 48);
      oled.print("%");

      oled.drawFastHLine(0, 56, OLED_W, SSD1306_WHITE);
      oled.setCursor(2, 58);
      oled.print("UP=back  SEL=retry");
      break;
    }
  }
  oled.display();
  delay(33);
}

// =============================================================================
//  TEMPERATURE (DS18B20)
// =============================================================================
enum TempState { T_WAIT, T_SHOW };
TempState tempState;
float tempSmoothBuf[5] = {0};
int   tempSmoothIdx = 0;

void tempEnter() {
  tempState = T_WAIT;
  tempSmoothIdx = 0;
  dsCalibrateBaseline();
}

void runTemp() {
  if (screenEntered) { tempEnter(); screenEntered = false; }

  if (btnUp.justPressed) {
    currentScreen = SCR_HOME; screenEntered = true; return;
  }

  // Request reading at most every 750ms (DS18B20 conversion time)
  static uint32_t lastRequest = 0;
  if (millis() - lastRequest > 750) {
    dsRequest();
    lastRequest = millis();
  }

  oled.clearDisplay();
  oled.setTextSize(1);
  oled.setTextColor(SSD1306_WHITE);

  switch (tempState) {
    case T_WAIT: {
      oled.setCursor(0, 0);
      oled.print("Body Temp");
      oled.drawFastHLine(0, 10, OLED_W, SSD1306_WHITE);

      drawThermometerAnim(OLED_W / 2, 30, phase01(1400));
      drawCenteredText("Press probe", 50);
      drawCenteredText("on skin", 58);

      if (dsProbeTouched()) {
        tempState = T_SHOW;
        tempSmoothIdx = 0;
      }
      break;
    }
    case T_SHOW: {
      float c = dsRead();
      if (!isnan(c) && c != DEVICE_DISCONNECTED_C) {
        tempSmoothBuf[tempSmoothIdx % 5] = c;
        tempSmoothIdx++;
      }
      float sum = 0;
      int n = min(tempSmoothIdx, 5);
      for (int i = 0; i < n; i++) sum += tempSmoothBuf[i];
      float smooth = (n > 0) ? sum / n : c;

      oled.setCursor(0, 0);
      oled.print("Body Temp");
      oled.drawFastHLine(0, 10, OLED_W, SSD1306_WHITE);

      // Big temperature
      char buf[16];
      snprintf(buf, sizeof(buf), "%.1f", smooth);
      oled.setTextSize(3);
      int w = strlen(buf) * 18;
      oled.setCursor((OLED_W - w - 12) / 2, 18);
      oled.print(buf);
      oled.setTextSize(2);
      oled.setCursor((OLED_W - w - 12) / 2 + w + 2, 22);
      oled.print((char)247);   // degree symbol
      oled.setTextSize(2);
      oled.setCursor((OLED_W - w - 12) / 2 + w + 14, 22);
      oled.print("C");

      // Status
      oled.setTextSize(1);
      const char* status;
      if      (smooth < 30.0f) status = "Low";
      else if (smooth < 34.5f) status = "Cool";
      else if (smooth < 36.5f) status = "Normal";
      else if (smooth < 37.5f) status = "Warm";
      else if (smooth < 38.5f) status = "Mild fever";
      else                     status = "High fever";
      drawCenteredText(status, 48);

      // Bottom hint
      oled.drawFastHLine(0, 56, OLED_W, SSD1306_WHITE);
      oled.setCursor(2, 58);
      oled.print("UP = back");

      // If probe lifted, go back to waiting
      if (!dsProbeTouched()) {
        tempState = T_WAIT;
      }
      break;
    }
  }

  oled.display();
  delay(50);
}

// =============================================================================
//  ECG
// =============================================================================
enum EcgState { ECG_WAIT, ECG_LIVE };
EcgState ecgState;

const int ECG_BUF_W = 120;
uint8_t ecgWave[ECG_BUF_W];
int ecgX = 0;
uint32_t ecgLastBeat = 0;
int ecgBpm = 0;

void ecgEnter() {
  ecgState = ECG_WAIT;
  ecgX = 0;
  for (int i = 0; i < ECG_BUF_W; i++) ecgWave[i] = 20;
}

void runECG() {
  if (screenEntered) { ecgEnter(); screenEntered = false; }
  if (btnUp.justPressed) {
    currentScreen = SCR_HOME; screenEntered = true; return;
  }

  oled.clearDisplay();
  oled.setTextSize(1);
  oled.setTextColor(SSD1306_WHITE);

  switch (ecgState) {
    case ECG_WAIT: {
      oled.setCursor(0, 0);
      oled.print("ECG");
      oled.drawFastHLine(0, 10, OLED_W, SSD1306_WHITE);
      drawEcgElectrodes(OLED_W / 2, 30, phase01(1400));
      drawCenteredText("Attach electrodes", 56);

      if (ecgLeadsConnected()) {
        ecgState = ECG_LIVE;
        for (int i = 0; i < ECG_BUF_W; i++) ecgWave[i] = 20;
        ecgX = 0;
      }
      break;
    }
    case ECG_LIVE: {
      if (!ecgLeadsConnected()) {
        ecgState = ECG_WAIT;
        break;
      }
      int raw = analogRead(ECG_OUTPUT);
      int y = map(raw, 1200, 2800, 40, 0);
      y = constrain(y, 0, 40);

      static int prevRaw = 0;
      if (prevRaw < 2500 && raw > 2500 && millis() - ecgLastBeat > 300) {
        uint32_t dt = millis() - ecgLastBeat;
        if (ecgLastBeat > 0) ecgBpm = 60000 / dt;
        ecgLastBeat = millis();
      }
      prevRaw = raw;

      ecgWave[ecgX] = y;
      ecgX = (ecgX + 1) % ECG_BUF_W;

      oled.setCursor(0, 0);
      oled.print("ECG LIVE");
      oled.setCursor(70, 0);
      oled.printf("HR:%3d", ecgBpm);
      oled.drawFastHLine(0, 10, OLED_W, SSD1306_WHITE);

      for (int i = 1; i < ECG_BUF_W; i++) {
        int idx0 = (ecgX + i - 1) % ECG_BUF_W;
        int idx1 = (ecgX + i) % ECG_BUF_W;
        oled.drawLine(3 + i - 1, 14 + ecgWave[idx0],
                      3 + i,     14 + ecgWave[idx1], SSD1306_WHITE);
      }

      oled.drawFastHLine(0, 56, OLED_W, SSD1306_WHITE);
      oled.setCursor(2, 58);
      oled.print("UP = back");
      break;
    }
  }

  oled.display();
  if (ecgState == ECG_LIVE) delay(8);
  else                       delay(40);
}

// =============================================================================
//  ABOUT
// =============================================================================
void runAbout() {
  if (screenEntered) {
    oled.clearDisplay();
    oled.setTextSize(1);
    oled.setTextColor(SSD1306_WHITE);
    oled.setCursor(0, 0);
    oled.print("About");
    oled.drawFastHLine(0, 10, OLED_W, SSD1306_WHITE);

    oled.setCursor(0, 14);
    oled.print("VitalScope v2.1");
    oled.setCursor(0, 24);
    oled.print("OLED + DS18B20");

    oled.setCursor(0, 36);
    oled.printf("MAX30102: %s", hasMax ? "OK" : "NO");
    oled.setCursor(0, 46);
    oled.printf("DS18B20:  %s", hasDs ? "OK" : "NO");

    oled.drawFastHLine(0, 56, OLED_W, SSD1306_WHITE);
    oled.setCursor(2, 58);
    oled.print("UP = back");
    oled.display();
    screenEntered = false;
  }

  if (btnUp.justPressed) {
    currentScreen = SCR_HOME; screenEntered = true;
  }
  delay(50);
}

// =============================================================================
//  SETUP / LOOP
// =============================================================================
void setup() {
  Serial.begin(115200);
  delay(100);
  Serial.println("\n=== VitalScope OLED + DS18B20 ===");

  pinMode(BTN_UP,     INPUT_PULLUP);
  pinMode(BTN_DOWN,   INPUT_PULLUP);
  pinMode(BTN_SELECT, INPUT_PULLUP);

  Wire.begin(I2C_SDA, I2C_SCL, 400000);
  delay(100);

  if (!oled.begin(SSD1306_SWITCHCAPVCC, OLED_ADDR)) {
    Serial.println("OLED not at 0x3C, trying 0x3D...");
    if (!oled.begin(SSD1306_SWITCHCAPVCC, 0x3D)) {
      Serial.println("OLED FAILED — check wiring");
      while (1) delay(100);
    }
  }
  oled.clearDisplay();
  oled.display();

  runBootSplash();
  sensorsInit();
  delay(400);

  currentScreen = SCR_HOME;
  screenEntered = true;
}

void loop() {
  updateButtons();

  switch (currentScreen) {
    case SCR_HOME:  runHome();  break;
    case SCR_HR:    runHR();    break;
    case SCR_TEMP:  runTemp();  break;
    case SCR_ECG:   runECG();   break;
    case SCR_ABOUT: runAbout(); break;
    default:
      currentScreen = SCR_HOME; screenEntered = true;
      break;
  }
}
