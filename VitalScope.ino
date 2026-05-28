/*
 * =============================================================================
 *  VitalScope — Portable Health Monitor
 *  ESP32 + 3.5" Parallel TFT + MAX30102 + MLX90614 + AD8232
 *  Final build — animated UI with touchscreen navigation
 * =============================================================================
 *
 *  Libraries required (install these first):
 *    - TFT_eSPI          (by Bodmer, via Library Manager)
 *    - SparkFun MAX3010x (via Library Manager)
 *    - Adafruit MLX90614 (via Library Manager)
 *    - s60sc Adafruit_TouchScreen (manual ZIP install from GitHub)
 *
 *  Before flashing:
 *    1. Replace TFT_eSPI/User_Setup.h with the VitalScope version
 *    2. Edit s60sc TouchScreen.h and uncomment #define ESP32_WIFI_TOUCH
 *
 *  Pin map: see wiring reference — this code uses those exact pins.
 * =============================================================================
 */

#include <TFT_eSPI.h>
#include <TouchScreen.h>
#include <Wire.h>
#include "MAX30105.h"
#include "spo2_algorithm.h"
#include <Adafruit_MLX90614.h>
#include <math.h>

// -----------------------------------------------------------------------------
//  PIN DEFINITIONS
// -----------------------------------------------------------------------------
#define I2C_SDA       23
#define I2C_SCL       19

#define ECG_OUTPUT    36    // VP — AD8232 OUTPUT
#define ECG_LO_POS    18    // AD8232 LO+
#define ECG_LO_NEG     0    // AD8232 LO-

// Touch — s60sc library uses GPIO 35/39 internally via ESP32_WIFI_TOUCH
// We still need to tell TouchScreen the digital pin assignments even though
// the actual analog reads happen on 35/39
#define TOUCH_YP       2    // LCD_RS (sampled via GPIO 39 in ESP32_WIFI_TOUCH mode)
#define TOUCH_XM      15    // LCD_WR (sampled via GPIO 35 in ESP32_WIFI_TOUCH mode)
#define TOUCH_YM      22    // LCD_RST — touch uses this only during touch-read cycles
#define TOUCH_XP      17    // LCD_CS  — touch uses this only during touch-read cycles

// Touch calibration — will be refined after calibration sketch is run
// These are sensible starting values for the typical MCUFRIEND 3.5" shield
#define TS_MIN_X      150
#define TS_MAX_X     3800
#define TS_MIN_Y      150
#define TS_MAX_Y     3800
#define TS_MINPRESSURE  200
#define TS_MAXPRESSURE 1000

// -----------------------------------------------------------------------------
//  THEME — Apple-inspired with friendly color accents (RGB565)
// -----------------------------------------------------------------------------
#define COL_BG          0xEF3D
#define COL_CARD        0xFFFF
#define COL_CARD_ALT    0xE73C
#define COL_TEXT        0x1084
#define COL_TEXT_SEC    0x8C71
#define COL_TEXT_TRI    0xC618
#define COL_BLUE        0x041F
#define COL_PINK        0xF9CA
#define COL_ORANGE      0xFCA0
#define COL_GREEN       0x3666
#define COL_RED         0xE987
#define COL_PINK_LITE   0xFF1B
#define COL_ORANGE_LITE 0xFF9C
#define COL_GREEN_LITE  0xE7AD
#define COL_BLUE_LITE   0xDEBE
#define COL_DARK_BG     0x1084

#define SCREEN_W  480
#define SCREEN_H  320
#define HEADER_H   44
#define FRAME_MS   33

// -----------------------------------------------------------------------------
//  GLOBALS
// -----------------------------------------------------------------------------
TFT_eSPI tft = TFT_eSPI();

// Touch sensor — 300 ohms is typical X-plate resistance for these shields
TouchScreen ts = TouchScreen(TOUCH_XP, TOUCH_YP, TOUCH_XM, TOUCH_YM, 300);

// Sensors
MAX30105              max30102;
Adafruit_MLX90614     mlx;

bool  hasMax = false;
bool  hasMlx = false;

enum ScreenId {
  SCR_BOOT, SCR_HOME, SCR_HR, SCR_IR, SCR_ECG, SCR_ABOUT, SCR_POWER_OFF
};

ScreenId currentScreen = SCR_BOOT;
bool screenEntered = true;

struct TouchPoint { int x; int y; bool pressed; };

// MAX30102 buffer
const int MAX_BUF = 100;
uint32_t irBuf[MAX_BUF];
uint32_t redBuf[MAX_BUF];
int bufIdx = 0;

// -----------------------------------------------------------------------------
//  TOUCH HANDLING
// -----------------------------------------------------------------------------
TouchPoint readTouch() {
  TouchPoint tp = {0, 0, false};
  TSPoint p = ts.getPoint();

  // TouchScreen library leaves the SHARED pins in input mode after reading.
  // We MUST restore them as outputs for the TFT to keep drawing.
  pinMode(TOUCH_XM, OUTPUT);
  pinMode(TOUCH_YP, OUTPUT);

  if (p.z > TS_MINPRESSURE && p.z < TS_MAXPRESSURE) {
    // Map raw touch values to screen coordinates.
    // Swap/flip axes depending on your specific panel — the values below
    // work for the most common MCUFRIEND 3.5" shield rotation 1.
    tp.x = map(p.y, TS_MIN_Y, TS_MAX_Y, 0, SCREEN_W);
    tp.y = map(p.x, TS_MAX_X, TS_MIN_X, 0, SCREEN_H);
    tp.x = constrain(tp.x, 0, SCREEN_W - 1);
    tp.y = constrain(tp.y, 0, SCREEN_H - 1);
    tp.pressed = true;
  }
  return tp;
}

bool touchedRegion(TouchPoint tp, int x, int y, int w, int h) {
  return tp.pressed && tp.x >= x && tp.x < x + w && tp.y >= y && tp.y < y + h;
}

// -----------------------------------------------------------------------------
//  SENSOR WRAPPERS
// -----------------------------------------------------------------------------
void sensorsInit() {
  Wire.begin(I2C_SDA, I2C_SCL, 400000);
  delay(100);

  if (max30102.begin(Wire, I2C_SPEED_FAST)) {
    max30102.setup(60, 4, 2, 100, 411, 4096);
    hasMax = true;
    Serial.println("[MAX30102] OK");
  } else {
    Serial.println("[MAX30102] NOT FOUND");
  }

  if (mlx.begin()) {
    hasMlx = true;
    Serial.println("[MLX90614] OK");
  } else {
    Serial.println("[MLX90614] NOT FOUND");
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

float mlxObject()  { return hasMlx ? mlx.readObjectTempC()  : NAN; }
float mlxAmbient() { return hasMlx ? mlx.readAmbientTempC() : NAN; }

bool mlxTargetPresent() {
  if (!hasMlx) return false;
  float obj = mlx.readObjectTempC();
  float amb = mlx.readAmbientTempC();
  if (isnan(obj) || isnan(amb)) return false;
  return (obj > amb + 2.0f) && obj > 25.0f && obj < 45.0f;
}

int ecgRaw() { return analogRead(ECG_OUTPUT); }

bool ecgLeadsConnected() {
  return digitalRead(ECG_LO_POS) == LOW && digitalRead(ECG_LO_NEG) == LOW;
}

// -----------------------------------------------------------------------------
//  UI HELPERS
// -----------------------------------------------------------------------------
float phase01(uint32_t periodMs) {
  return (float)(millis() % periodMs) / (float)periodMs;
}

void drawCenteredText(const char* txt, int cx, int cy, uint8_t font,
                      uint16_t color, uint16_t bg) {
  tft.setTextColor(color, bg);
  tft.setTextDatum(MC_DATUM);
  tft.drawString(txt, cx, cy, font);
  tft.setTextDatum(TL_DATUM);
}

void drawCenteredFreeFont(const char* txt, int cx, int cy,
                          const GFXfont* font, uint16_t color, uint16_t bg) {
  tft.setTextColor(color, bg);
  tft.setFreeFont(font);
  tft.setTextDatum(MC_DATUM);
  tft.drawString(txt, cx, cy);
  tft.setTextDatum(TL_DATUM);
  tft.setFreeFont(NULL);
}

void drawHeader(const char* title, bool showBack = true) {
  tft.fillRect(0, 0, SCREEN_W, HEADER_H, COL_BG);
  if (showBack) {
    int bx = 16, by = HEADER_H / 2;
    tft.fillTriangle(bx, by, bx + 10, by - 9, bx + 10, by + 9, COL_BLUE);
    tft.setTextColor(COL_BLUE, COL_BG);
    tft.setTextDatum(ML_DATUM);
    tft.drawString("Back", 32, by, 2);
    tft.setTextDatum(TL_DATUM);
  }
  drawCenteredText(title, SCREEN_W / 2, HEADER_H / 2, 4, COL_TEXT, COL_BG);
  tft.drawFastHLine(0, HEADER_H, SCREEN_W, COL_TEXT_TRI);
}

bool backButtonTouched(TouchPoint tp) {
  return touchedRegion(tp, 0, 0, 90, HEADER_H);
}

void drawProgressBar(int x, int y, int w, int h, float p, uint16_t color) {
  p = constrain(p, 0.0f, 1.0f);
  tft.fillRoundRect(x, y, w, h, h / 2, COL_CARD_ALT);
  int fillW = (int)((w - 4) * p);
  if (fillW > 0) tft.fillRoundRect(x + 2, y + 2, fillW, h - 4, (h - 4) / 2, color);
}

// -----------------------------------------------------------------------------
//  ANIMATION PRIMITIVES
// -----------------------------------------------------------------------------
void drawFingerAnim(int cx, int cy, float phase) {
  float s = 1.0f + 0.08f * sinf(phase * 2.0f * PI);
  int rX = (int)(28 * s);
  int rY = (int)(55 * s);

  tft.fillRect(cx - 40, cy - 70, 80, 140, COL_BG);
  tft.fillEllipse(cx, cy + 10, rX, rY, COL_PINK_LITE);
  tft.fillEllipse(cx, cy - 28, rX - 3, (int)(rY * 0.4f), COL_PINK_LITE);
  tft.drawEllipse(cx, cy + 10, rX, rY, COL_PINK);
}

void drawHeartFill(int cx, int cy, int size, float fill) {
  fill = constrain(fill, 0.0f, 1.0f);
  int r = size / 2;
  int topY = cy - size / 2;

  tft.fillRect(cx - size, cy - size, size * 2, size * 2, COL_BG);

  tft.drawCircle(cx - r / 2, topY + r / 2, r / 2, COL_PINK);
  tft.drawCircle(cx + r / 2, topY + r / 2, r / 2, COL_PINK);
  tft.drawLine(cx - r, topY + r / 2, cx, cy + r, COL_PINK);
  tft.drawLine(cx + r, topY + r / 2, cx, cy + r, COL_PINK);

  int fillTopY = cy + r - (int)(fill * size);
  for (int y = fillTopY; y <= cy + r; y++) {
    int dy = y - topY;
    int halfWidth;
    if (dy < r) {
      int cy1 = topY + r / 2;
      int d = abs(y - cy1);
      if (d > r / 2) continue;
      halfWidth = r / 2 + (int)sqrtf((float)(r * r / 4 - d * d));
    } else {
      halfWidth = (int)((float)r * (1.0f - (float)(dy - r) / r));
    }
    tft.drawFastHLine(cx - halfWidth, y, halfWidth * 2, COL_PINK);
  }
}

void drawIrAimAnim(int cx, int cy, float phase) {
  tft.fillRect(cx - 70, cy - 70, 140, 140, COL_BG);
  float s = 0.5f + 0.5f * sinf(phase * 2.0f * PI);
  int ringR = 45 + (int)(s * 15);
  tft.drawCircle(cx, cy, ringR, COL_ORANGE);
  tft.drawCircle(cx, cy, ringR - 1, COL_ORANGE);
  tft.drawCircle(cx, cy, ringR + 8, COL_ORANGE_LITE);
  tft.fillCircle(cx, cy, 30, COL_ORANGE_LITE);
  tft.drawLine(cx - 14, cy, cx + 14, cy, COL_ORANGE);
  tft.drawLine(cx, cy - 14, cx, cy + 14, COL_ORANGE);
  tft.drawCircle(cx, cy, 6, COL_ORANGE);
}

void drawEcgElectrodes(int cx, int cy, float phase) {
  tft.fillRect(cx - 80, cy - 50, 160, 100, COL_BG);
  float s = 0.5f + 0.5f * sinf(phase * 2.0f * PI);
  int rOuter = 20 + (int)(s * 10);
  int pts[3][2] = { {cx - 55, cy - 20}, {cx + 55, cy - 20}, {cx, cy + 30} };
  uint16_t cols[3] = { COL_RED, COL_ORANGE, COL_GREEN };
  const char* lbl[3] = { "RA", "LA", "RL" };

  for (int i = 0; i < 3; i++) {
    tft.drawCircle(pts[i][0], pts[i][1], rOuter, cols[i]);
    tft.fillCircle(pts[i][0], pts[i][1], 14, cols[i]);
    tft.setTextColor(COL_CARD, cols[i]);
    tft.setTextDatum(MC_DATUM);
    tft.drawString(lbl[i], pts[i][0], pts[i][1], 2);
    tft.setTextDatum(TL_DATUM);
  }
}

void drawSpinner(int cx, int cy, int r, float phase, uint16_t color) {
  tft.drawCircle(cx, cy, r, COL_TEXT_TRI);
  float startA = phase * 2.0f * PI;
  float span = PI * 1.2f;
  const int steps = 40;
  for (int i = 0; i < steps; i++) {
    float a = startA + span * ((float)i / steps);
    int x0 = cx + (int)(cosf(a) * r);
    int y0 = cy + (int)(sinf(a) * r);
    int x1 = cx + (int)(cosf(a) * (r - 3));
    int y1 = cy + (int)(sinf(a) * (r - 3));
    tft.drawLine(x0, y0, x1, y1, color);
  }
}

// -----------------------------------------------------------------------------
//  BOOT SPLASH
// -----------------------------------------------------------------------------
void runBootSplash() {
  tft.fillScreen(COL_BG);
  uint32_t start = millis();
  const uint32_t duration = 2800;
  const int cx = SCREEN_W / 2, cy = 130;

  const char* steps[] = { "Initializing display...", "Checking I2C bus...",
                          "Detecting sensors...", "Ready" };
  int stepIdx = 0;

  drawCenteredText("VitalScope", cx, 220, 4, COL_TEXT, COL_BG);
  drawCenteredText("Portable Health Monitor", cx, 250, 2, COL_TEXT_SEC, COL_BG);

  while (millis() - start < duration) {
    float phase = phase01(1200);
    drawSpinner(cx, cy, 55, phase, COL_BLUE);
    tft.fillCircle(cx - 10, cy - 5, 12, COL_PINK);
    tft.fillCircle(cx + 10, cy - 5, 12, COL_PINK);
    tft.fillTriangle(cx - 21, cy, cx + 21, cy, cx, cy + 24, COL_PINK);

    int newStep = (millis() - start) / (duration / 4);
    if (newStep >= 4) newStep = 3;
    if (newStep != stepIdx) {
      stepIdx = newStep;
      tft.fillRect(40, 275, SCREEN_W - 80, 25, COL_BG);
    }
    drawCenteredText(steps[stepIdx], cx, 288, 2, COL_TEXT_SEC, COL_BG);
    delay(FRAME_MS);
  }
}

// -----------------------------------------------------------------------------
//  HOME SCREEN
// -----------------------------------------------------------------------------
struct HomeTile {
  int x, y, w, h;
  uint16_t fill, iconBg, iconFg;
  const char* title;
  const char* sub;
  ScreenId target;
  char icon;
};

const HomeTile TILES[] = {
  {  16,  56, 224, 112, COL_CARD, COL_PINK_LITE,   COL_PINK,   "Heart Rate",  "Pulse & SpO2",     SCR_HR,    'H' },
  { 240,  56, 224, 112, COL_CARD, COL_ORANGE_LITE, COL_ORANGE, "Body Temp",   "Contactless IR",   SCR_IR,    'I' },
  {  16, 176, 224, 112, COL_CARD, COL_BLUE_LITE,   COL_BLUE,   "ECG",         "Heart waveform",   SCR_ECG,   'E' },
  { 240, 176, 224, 112, COL_CARD, COL_GREEN_LITE,  COL_GREEN,  "About",       "System info",      SCR_ABOUT, 'A' }
};
const int NUM_TILES = 4;

void drawTileIcon(const HomeTile& t) {
  int ix = t.x + 44, iy = t.y + t.h / 2;
  tft.fillCircle(ix, iy, 26, t.iconBg);
  switch (t.icon) {
    case 'H':
      tft.fillCircle(ix - 7, iy - 3, 7, t.iconFg);
      tft.fillCircle(ix + 7, iy - 3, 7, t.iconFg);
      tft.fillTriangle(ix - 13, iy, ix + 13, iy, ix, iy + 13, t.iconFg);
      break;
    case 'I':
      tft.drawCircle(ix, iy, 13, t.iconFg);
      tft.drawCircle(ix, iy, 7,  t.iconFg);
      tft.fillCircle(ix, iy, 2,  t.iconFg);
      break;
    case 'E':
      tft.drawLine(ix - 14, iy,      ix - 6, iy, t.iconFg);
      tft.drawLine(ix - 6,  iy,      ix - 3, iy - 10, t.iconFg);
      tft.drawLine(ix - 3,  iy - 10, ix + 2, iy + 10, t.iconFg);
      tft.drawLine(ix + 2,  iy + 10, ix + 6, iy,      t.iconFg);
      tft.drawLine(ix + 6,  iy,      ix + 14, iy,     t.iconFg);
      break;
    case 'A':
      tft.drawCircle(ix, iy, 14, t.iconFg);
      tft.setTextColor(t.iconFg, t.iconBg);
      tft.setTextDatum(MC_DATUM);
      tft.drawString("i", ix, iy, 4);
      tft.setTextDatum(TL_DATUM);
      break;
  }
}

void drawHome() {
  tft.fillScreen(COL_BG);
  tft.setTextDatum(TL_DATUM);
  tft.setTextColor(COL_TEXT, COL_BG);
  tft.drawString("Health", 16, 14, 4);
  tft.setTextColor(COL_TEXT_SEC, COL_BG);
  tft.drawString("Select a measurement", 16, 40, 2);

  // Power off button (top right)
  tft.fillCircle(SCREEN_W - 24, 24, 16, COL_CARD);
  tft.drawCircle(SCREEN_W - 24, 24, 16, COL_RED);
  tft.drawLine(SCREEN_W - 24, 14, SCREEN_W - 24, 24, COL_RED);
  tft.drawCircle(SCREEN_W - 24, 26, 6, COL_RED);

  for (int i = 0; i < NUM_TILES; i++) {
    const HomeTile& t = TILES[i];
    tft.fillRoundRect(t.x, t.y, t.w, t.h, 14, t.fill);
    drawTileIcon(t);
    tft.setTextColor(COL_TEXT, t.fill);
    tft.drawString(t.title, t.x + 84, t.y + 36, 4);
    tft.setTextColor(COL_TEXT_SEC, t.fill);
    tft.drawString(t.sub, t.x + 84, t.y + 64, 2);
  }
}

void runHome() {
  if (screenEntered) { drawHome(); screenEntered = false; }

  TouchPoint tp = readTouch();
  if (tp.pressed) {
    // Power off?
    if (abs(tp.x - (SCREEN_W - 24)) < 22 && abs(tp.y - 24) < 22) {
      currentScreen = SCR_POWER_OFF;
      screenEntered = true;
      delay(200);
      return;
    }
    for (int i = 0; i < NUM_TILES; i++) {
      const HomeTile& tl = TILES[i];
      if (touchedRegion(tp, tl.x, tl.y, tl.w, tl.h)) {
        currentScreen = tl.target;
        screenEntered = true;
        delay(200);
        return;
      }
    }
  }
  delay(30);
}

// -----------------------------------------------------------------------------
//  HEART RATE / SpO2 SCREEN
// -----------------------------------------------------------------------------
enum HRState { HR_WAIT, HR_MEASURE, HR_DONE };
HRState hrState;
int32_t hrBpm, hrSpo2;

void hrEnter() {
  tft.fillScreen(COL_BG);
  drawHeader("Heart Rate & SpO2");
  hrState = HR_WAIT;
  maxReset();
}

void hrDrawResults(bool valid) {
  tft.fillRect(0, HEADER_H + 1, SCREEN_W, SCREEN_H - HEADER_H - 1, COL_BG);

  // HR card
  tft.fillRoundRect(24, 66, 210, 210, 12, COL_CARD);
  tft.fillCircle(128, 110, 16, COL_PINK_LITE);
  tft.fillCircle(121, 108, 7, COL_PINK);
  tft.fillCircle(135, 108, 7, COL_PINK);
  tft.fillTriangle(115, 112, 141, 112, 128, 124, COL_PINK);

  drawCenteredText(valid ? String(hrBpm).c_str() : "--",
                   128, 175, 7, COL_TEXT, COL_CARD);
  drawCenteredText("BPM", 128, 215, 2, COL_TEXT_SEC, COL_CARD);

  const char* hrStatus = "--"; uint16_t hrCol = COL_TEXT_SEC;
  if (valid) {
    if (hrBpm < 60)      { hrStatus = "Low";    hrCol = COL_ORANGE; }
    else if (hrBpm > 100){ hrStatus = "High";   hrCol = COL_ORANGE; }
    else                 { hrStatus = "Normal"; hrCol = COL_GREEN;  }
  }
  drawCenteredText(hrStatus, 128, 250, 2, hrCol, COL_CARD);

  // SpO2 card
  tft.fillRoundRect(246, 66, 210, 210, 12, COL_CARD);
  tft.drawCircle(351, 110, 16, COL_GREEN);
  tft.drawCircle(351, 110, 15, COL_GREEN);
  drawCenteredText("O2", 351, 110, 2, COL_GREEN, COL_CARD);

  drawCenteredText(valid ? String(hrSpo2).c_str() : "--",
                   351, 175, 7, COL_TEXT, COL_CARD);
  drawCenteredText("% SpO2", 351, 215, 2, COL_TEXT_SEC, COL_CARD);

  const char* spStatus = "--"; uint16_t spCol = COL_TEXT_SEC;
  if (valid) {
    if (hrSpo2 < 90)       { spStatus = "Low";       spCol = COL_RED;    }
    else if (hrSpo2 < 95)  { spStatus = "Fair";      spCol = COL_ORANGE; }
    else                   { spStatus = "Excellent"; spCol = COL_GREEN;  }
  }
  drawCenteredText(spStatus, 351, 250, 2, spCol, COL_CARD);

  // Retake button
  tft.fillRoundRect(SCREEN_W/2 - 70, 290, 140, 24, 12, COL_BLUE);
  drawCenteredText("Measure again", SCREEN_W/2, 302, 2, COL_CARD, COL_BLUE);
}

void runHR() {
  if (screenEntered) { hrEnter(); screenEntered = false; }

  TouchPoint tp = readTouch();
  if (backButtonTouched(tp)) {
    currentScreen = SCR_HOME; screenEntered = true; delay(200); return;
  }

  switch (hrState) {
    case HR_WAIT: {
      float phase = phase01(1200);
      drawFingerAnim(SCREEN_W / 2, 140, phase);
      static uint32_t lastTxt = 0;
      if (millis() - lastTxt > 500) {
        tft.fillRect(0, 220, SCREEN_W, 80, COL_BG);
        drawCenteredText("Place your finger on the sensor",
                         SCREEN_W / 2, 235, 4, COL_TEXT, COL_BG);
        drawCenteredText("Keep still until reading completes",
                         SCREEN_W / 2, 265, 2, COL_TEXT_SEC, COL_BG);
        lastTxt = millis();
      }
      if (maxFingerPresent()) {
        hrState = HR_MEASURE;
        maxReset();
        tft.fillRect(0, HEADER_H + 1, SCREEN_W, SCREEN_H - HEADER_H - 1, COL_BG);
      }
      delay(FRAME_MS);
      break;
    }
    case HR_MEASURE: {
      bool full = maxPollOne();
      float p = maxProgress();
      drawHeartFill(SCREEN_W / 2, 140, 140, p);

      static uint32_t lastBar = 0;
      if (millis() - lastBar > 100) {
        tft.fillRect(60, 240, SCREEN_W - 120, 50, COL_BG);
        drawProgressBar(80, 250, SCREEN_W - 160, 10, p, COL_PINK);
        char pct[32];
        snprintf(pct, sizeof(pct), "Analyzing pulse... %d%%", (int)(p * 100));
        drawCenteredText(pct, SCREEN_W / 2, 278, 2, COL_TEXT_SEC, COL_BG);
        lastBar = millis();
      }
      if (!maxFingerPresent() && p < 0.5f) {
        hrState = HR_WAIT;
        tft.fillRect(0, HEADER_H + 1, SCREEN_W, SCREEN_H - HEADER_H - 1, COL_BG);
        break;
      }
      if (full) {
        bool ok = maxCompute(hrBpm, hrSpo2);
        hrDrawResults(ok);
        hrState = HR_DONE;
      }
      delay(FRAME_MS);
      break;
    }
    case HR_DONE: {
      if (touchedRegion(tp, SCREEN_W/2 - 70, 290, 140, 24)) {
        hrEnter();
        delay(200);
      }
      delay(50);
      break;
    }
  }
}

// -----------------------------------------------------------------------------
//  IR TEMPERATURE SCREEN
// -----------------------------------------------------------------------------
enum IRState { IR_WAIT, IR_SHOW };
IRState irState;
float irSmoothBuf[5] = {0};
int irSmoothIdx = 0;

void irEnter() {
  tft.fillScreen(COL_BG);
  drawHeader("Body Temp (IR)");
  irState = IR_WAIT;
  irSmoothIdx = 0;
}

void runIR() {
  if (screenEntered) { irEnter(); screenEntered = false; }

  TouchPoint tp = readTouch();
  if (backButtonTouched(tp)) {
    currentScreen = SCR_HOME; screenEntered = true; delay(200); return;
  }

  switch (irState) {
    case IR_WAIT: {
      float phase = phase01(1400);
      drawIrAimAnim(SCREEN_W / 2, 140, phase);
      static uint32_t lastTxt = 0;
      if (millis() - lastTxt > 400) {
        tft.fillRect(0, 220, SCREEN_W, 80, COL_BG);
        drawCenteredText("Aim at your forehead",
                         SCREEN_W / 2, 240, 4, COL_TEXT, COL_BG);
        drawCenteredText("Hold 3-5 cm away - stay still",
                         SCREEN_W / 2, 270, 2, COL_TEXT_SEC, COL_BG);
        lastTxt = millis();
      }
      if (mlxTargetPresent()) {
        irState = IR_SHOW;
        irSmoothIdx = 0;
        tft.fillRect(0, HEADER_H + 1, SCREEN_W, SCREEN_H - HEADER_H - 1, COL_BG);
      }
      delay(FRAME_MS);
      break;
    }
    case IR_SHOW: {
      float obj = mlxObject();
      float amb = mlxAmbient();
      irSmoothBuf[irSmoothIdx % 5] = obj;
      irSmoothIdx++;
      float sum = 0;
      int n = min(irSmoothIdx, 5);
      for (int i = 0; i < n; i++) sum += irSmoothBuf[i];
      float smooth = sum / n;

      static uint32_t lastDraw = 0;
      if (millis() - lastDraw > 250) {
        tft.fillRoundRect(30, 70, SCREEN_W - 60, 180, 12, COL_CARD);
        drawCenteredText("Forehead Temperature", SCREEN_W/2, 100, 4,
                         COL_ORANGE, COL_CARD);

        char sBuf[16];
        snprintf(sBuf, sizeof(sBuf), "%.1f C", smooth);
        drawCenteredText(sBuf, SCREEN_W/2, 165, 7, COL_TEXT, COL_CARD);

        const char* status = "--"; uint16_t col = COL_TEXT_SEC;
        if      (smooth < 35.5f) { status = "Low";        col = COL_BLUE;   }
        else if (smooth < 37.5f) { status = "Normal";     col = COL_GREEN;  }
        else if (smooth < 38.5f) { status = "Mild fever"; col = COL_ORANGE; }
        else                     { status = "High fever"; col = COL_RED;    }
        drawCenteredText(status, SCREEN_W/2, 205, 4, col, COL_CARD);

        char ambBuf[32];
        snprintf(ambBuf, sizeof(ambBuf), "Ambient: %.1f C", amb);
        drawCenteredText(ambBuf, SCREEN_W/2, 235, 2, COL_TEXT_SEC, COL_CARD);
        lastDraw = millis();
      }
      if (!mlxTargetPresent()) {
        static uint32_t lostSince = millis();
        if (millis() - lostSince > 1500) {
          irState = IR_WAIT;
          tft.fillRect(0, HEADER_H + 1, SCREEN_W, SCREEN_H - HEADER_H - 1, COL_BG);
        }
      }
      delay(FRAME_MS);
      break;
    }
  }
}

// -----------------------------------------------------------------------------
//  ECG SCREEN
// -----------------------------------------------------------------------------
enum ECGState { ECG_WAIT, ECG_LIVE };
ECGState ecgState;
int ecgX, ecgLastY;
uint32_t ecgLastBeat = 0;
int ecgBpm = 0;

void ecgEnter() {
  tft.fillScreen(COL_BG);
  drawHeader("ECG Waveform");
  ecgState = ECG_WAIT;
  ecgX = 15;
  ecgLastY = 170;
}

void ecgDrawPlotFrame() {
  tft.fillRoundRect(8, 56, SCREEN_W - 16, 210, 8, COL_CARD);
  for (int y = 80; y < 260; y += 30) tft.drawFastHLine(10, y, SCREEN_W - 20, 0xF7BE);
  for (int x = 20; x < SCREEN_W - 20; x += 30) tft.drawFastVLine(x, 58, 206, 0xF7BE);
}

void runECG() {
  if (screenEntered) { ecgEnter(); screenEntered = false; }

  TouchPoint tp = readTouch();
  if (backButtonTouched(tp)) {
    currentScreen = SCR_HOME; screenEntered = true; delay(200); return;
  }

  switch (ecgState) {
    case ECG_WAIT: {
      float phase = phase01(1400);
      drawEcgElectrodes(SCREEN_W / 2, 140, phase);
      static uint32_t lastTxt = 0;
      if (millis() - lastTxt > 400) {
        tft.fillRect(0, 220, SCREEN_W, 80, COL_BG);
        drawCenteredText("Attach all 3 electrodes",
                         SCREEN_W / 2, 240, 4, COL_TEXT, COL_BG);
        drawCenteredText("RA = right arm    LA = left arm    RL = right leg",
                         SCREEN_W / 2, 270, 2, COL_TEXT_SEC, COL_BG);
        lastTxt = millis();
      }
      if (ecgLeadsConnected()) {
        ecgState = ECG_LIVE;
        tft.fillRect(0, HEADER_H + 1, SCREEN_W, SCREEN_H - HEADER_H - 1, COL_BG);
        ecgDrawPlotFrame();
        ecgX = 15;
      }
      delay(FRAME_MS);
      break;
    }
    case ECG_LIVE: {
      if (!ecgLeadsConnected()) {
        ecgState = ECG_WAIT;
        tft.fillRect(0, HEADER_H + 1, SCREEN_W, SCREEN_H - HEADER_H - 1, COL_BG);
        break;
      }
      int raw = ecgRaw();
      int y = map(raw, 1200, 2800, 260, 62);
      y = constrain(y, 62, 260);

      static int prevRaw = 0;
      if (prevRaw < 2500 && raw > 2500 && millis() - ecgLastBeat > 300) {
        uint32_t dt = millis() - ecgLastBeat;
        if (ecgLastBeat > 0) ecgBpm = 60000 / dt;
        ecgLastBeat = millis();
      }
      prevRaw = raw;

      tft.drawLine(ecgX - 1, ecgLastY, ecgX, y, COL_GREEN);
      ecgLastY = y;
      ecgX++;

      if (ecgX + 4 < SCREEN_W - 10)
        tft.fillRect(ecgX, 58, 4, 206, COL_CARD);

      if (ecgX >= SCREEN_W - 10) {
        ecgX = 15;
        ecgDrawPlotFrame();
      }

      static uint32_t lastHud = 0;
      if (millis() - lastHud > 500) {
        tft.fillRect(0, 275, SCREEN_W, 45, COL_BG);
        char s[32];
        snprintf(s, sizeof(s), "HR: %d BPM", ecgBpm);
        tft.setTextColor(COL_TEXT, COL_BG);
        tft.setTextDatum(TL_DATUM);
        tft.drawString(s, 16, 285, 4);
        drawCenteredText("LIVE", SCREEN_W - 50, 296, 2, COL_GREEN, COL_BG);
        lastHud = millis();
      }
      delay(5);
      break;
    }
  }
}

// -----------------------------------------------------------------------------
//  ABOUT SCREEN
// -----------------------------------------------------------------------------
void runAbout() {
  if (screenEntered) {
    tft.fillScreen(COL_BG);
    drawHeader("About");
    tft.fillRoundRect(16, 60, SCREEN_W - 32, 230, 12, COL_CARD);

    tft.setTextDatum(TL_DATUM);
    tft.setTextColor(COL_TEXT, COL_CARD);
    tft.drawString("VitalScope v1.0", 32, 76, 4);

    tft.setTextColor(COL_TEXT_SEC, COL_CARD);
    tft.drawString("Portable multi-sensor health monitor", 32, 102, 2);

    tft.setTextColor(COL_TEXT, COL_CARD);
    tft.drawString("Sensors", 32, 130, 2);

    tft.setTextColor(COL_TEXT_SEC, COL_CARD);
    char buf[48];
    snprintf(buf, sizeof(buf), "MAX30102  - %s", hasMax ? "OK" : "missing");
    tft.drawString(buf, 48, 150, 2);
    snprintf(buf, sizeof(buf), "MLX90614  - %s", hasMlx ? "OK" : "missing");
    tft.drawString(buf, 48, 170, 2);
    tft.drawString("AD8232    - analog (always present)", 48, 190, 2);

    tft.setTextColor(COL_RED, COL_CARD);
    tft.drawString("For educational use only.", 32, 220, 2);
    tft.drawString("Not a medical device.", 32, 240, 2);
    screenEntered = false;
  }

  TouchPoint tp = readTouch();
  if (backButtonTouched(tp)) {
    currentScreen = SCR_HOME; screenEntered = true; delay(200);
  }
  delay(50);
}

// -----------------------------------------------------------------------------
//  POWER OFF
// -----------------------------------------------------------------------------
void runPowerOff() {
  tft.fillScreen(COL_DARK_BG);
  drawCenteredText("Powering off...", SCREEN_W/2, SCREEN_H/2 + 40,
                   4, TFT_WHITE, COL_DARK_BG);
  drawCenteredText("It is safe to disconnect", SCREEN_W/2, SCREEN_H/2 + 70,
                   2, COL_TEXT_SEC, COL_DARK_BG);

  uint32_t start = millis();
  while (millis() - start < 3500) {
    float phase = phase01(1500);
    int cy = SCREEN_H/2 - 30;
    int cx = SCREEN_W/2;
    int r = 32 + (int)(sinf(phase * 2.0f * PI) * 4);
    tft.fillCircle(cx, cy, 36, COL_DARK_BG);
    tft.drawCircle(cx, cy, r, COL_ORANGE);
    tft.drawCircle(cx, cy, r - 1, COL_ORANGE);
    tft.drawFastVLine(cx, cy - 16, 18, COL_ORANGE);
    tft.drawFastVLine(cx - 1, cy - 16, 18, COL_ORANGE);
    tft.drawFastVLine(cx + 1, cy - 16, 18, COL_ORANGE);
    delay(FRAME_MS);
  }

  tft.fillScreen(TFT_BLACK);
  Serial.println("Entering deep sleep...");
  delay(200);
  esp_deep_sleep_start();
}

// -----------------------------------------------------------------------------
//  SETUP / LOOP
// -----------------------------------------------------------------------------
void setup() {
  Serial.begin(115200);
  delay(100);
  Serial.println("\n=== VitalScope booting ===");

  tft.init();
  tft.setRotation(1);  // Landscape 480 x 320

  runBootSplash();
  sensorsInit();
  delay(500);

  currentScreen = SCR_HOME;
  screenEntered = true;
}

void loop() {
  switch (currentScreen) {
    case SCR_HOME:      runHome();       break;
    case SCR_HR:        runHR();         break;
    case SCR_IR:        runIR();         break;
    case SCR_ECG:       runECG();        break;
    case SCR_ABOUT:     runAbout();      break;
    case SCR_POWER_OFF: runPowerOff();   break;
    default:
      currentScreen = SCR_HOME; screenEntered = true;
      break;
  }
}
