/// @file    jasper_lights.ino
/// @brief   Simple M5StickC Plus2 LED blinker for Jasper
/// @version 1.1.0
/// @date    2026-09-26
/// @author  John Cohn (patterns adapted from m5lights_v1 / Larry's patterns)
///
/// A button (big front button): next pattern (patterns cross-fade)
/// B button (side button): cycle through 6 brightness levels
///
/// @changelog
/// v1.1.0 - Added baby-friendly patterns (B&W Stripes, Starry Night, White Comet,
///          Breathe, Pastel Twinkle), B button brightness, removed Wavy Flag
/// v1.0.0 - Initial version stripped down from m5lights_v1

#include <M5StickCPlus2.h>
#include <FastLED.h>

#define VERSION "1.1.0"

// Hardware config
#define LED_PIN 32
#define NUM_LEDS 200
#define COLOR_ORDER GRB
#define CHIPSET WS2811

#define FADE_DURATION_MS 1500  // Cross-fade time between patterns
#define FRAME_MS 16            // ~60 FPS

// Brightness levels cycled by the B button. 25 was the m5lights_v1 default,
// chosen for M5Stick 5V power stability -- the top levels draw a lot more
// current, especially on the all-white patterns.
const uint8_t brightnessLevels[] = { 4, 8, 15, 25, 40, 60 };
#define NUM_BRIGHTNESS_LEVELS (sizeof(brightnessLevels) / sizeof(brightnessLevels[0]))
uint8_t brightnessIndex = 3;  // Start at 25

CRGB leds[NUM_LEDS];

// ===== GAMMA CORRECTION =====
// Extends black range (0-41 -> pure black) for dramatic dark gaps and rich colors
const byte gammaTable[256] = {
    0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,
    0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,
    0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  1,  1,  1,  1,  2,  2,
    3,  3,  4,  4,  4,  4,  5,  5,  5,  5,  6,  6,  6,  7,  7,  7,
    8,  8,  8,  9,  9,  9, 10, 10, 11, 11, 11, 12, 12, 13, 13, 14,
   14, 15, 15, 16, 16, 17, 17, 18, 18, 19, 19, 20, 20, 21, 22, 22,
   23, 23, 24, 25, 25, 26, 26, 27, 28, 28, 29, 30, 30, 31, 32, 33,
   33, 34, 35, 35, 36, 37, 38, 38, 39, 40, 41, 42, 42, 43, 44, 45,
   46, 47, 47, 48, 49, 50, 51, 52, 53, 54, 55, 56, 56, 57, 58, 59,
   60, 61, 62, 63, 64, 65, 66, 67, 68, 70, 71, 72, 73, 74, 75, 76,
   77, 78, 79, 81, 82, 83, 84, 85, 86, 88, 89, 90, 91, 92, 94, 95,
   96, 97, 99,100,101,102,104,105,106,108,109,110,112,113,114,116,
  117,119,120,121,123,124,126,127,129,130,132,133,135,136,138,139,
  141,142,144,145,147,149,150,152,153,155,157,158,160,162,163,165,
  167,168,170,172,174,175,177,179,181,182,184,186,188,190,192,193,
  195,197,199,201,203,205,207,209,211,213,215,217,219,221,223,225
};

CRGB gammaRGB(byte r, byte g, byte b) {
  return CRGB(gammaTable[r], gammaTable[g], gammaTable[b]);
}

// ===== FIXED-POINT MATH =====
// Sine lookup table (0-180). MUST be signed char: ESP32 'char' is unsigned,
// which breaks the sine wave troughs (no dark gaps).
const signed char sineTable[181] = {
  0,1,2,3,5,6,7,8,9,10,11,12,13,15,16,17,
  18,19,20,21,22,23,24,25,27,28,29,30,31,32,33,34,
  35,36,37,38,39,40,42,43,44,45,46,47,48,49,50,51,
  52,53,54,55,56,57,58,59,60,61,62,63,64,65,66,67,
  67,68,69,70,71,72,73,74,75,76,77,77,78,79,80,81,
  82,83,83,84,85,86,87,88,88,89,90,91,92,92,93,94,
  95,95,96,97,97,98,99,100,100,101,102,102,103,104,104,105,
  105,106,107,107,108,108,109,110,110,111,111,112,112,113,113,114,
  114,115,115,116,116,117,117,117,118,118,119,119,120,120,120,121,
  121,121,122,122,122,123,123,123,123,124,124,124,124,125,125,125,
  125,125,126,126,126,126,126,126,126,127,127,127,127,127,127,127,
  127,127,127,127,127
};

// Angle in 720 units per cycle; returns -127..+127
signed char fixSin(int angle) {
  angle %= 720;
  if (angle < 0) angle += 720;
  return (angle <= 360) ?
     sineTable[(angle <= 180) ? angle : (360 - angle)] :
    -sineTable[(angle <= 540) ? (angle - 360) : (720 - angle)];
}

signed char fixCos(int angle) {
  angle %= 720;
  if (angle < 0) angle += 720;
  return (angle <= 360) ?
    ((angle <= 180) ?  sineTable[180 - angle]  :
                      -sineTable[angle - 180]) :
    ((angle <= 540) ? -sineTable[540 - angle]  :
                       sineTable[angle - 540]);
}

// HSV to RGB with 1536 hue units (smoother than FastLED's 256)
void hsvToRgb(int h, byte s, byte v, byte *r, byte *g, byte *b) {
  h %= 1536;
  if (h < 0) h += 1536;

  byte sextant = h >> 8;
  byte frac = h & 255;
  byte vs = (v * s) >> 8;
  byte p = v - vs;
  byte q = v - ((vs * frac) >> 8);
  byte t = v - ((vs * (255 - frac)) >> 8);

  switch (sextant) {
    case 0: *r = v; *g = t; *b = p; break;
    case 1: *r = q; *g = v; *b = p; break;
    case 2: *r = p; *g = v; *b = t; break;
    case 3: *r = p; *g = q; *b = v; break;
    case 4: *r = t; *g = p; *b = v; break;
    default: *r = v; *g = p; *b = q; break;
  }
}

// ===== PATTERNS =====
// Each pattern draws one frame into leds[]. 'reset' is true on the first frame
// after the pattern is selected, so it can pick fresh random parameters.

// Pattern 0: Solid color slowly fading through the rainbow
void solidColor(bool reset) {
  static int hue = 0;
  if (reset) hue = random(1536);

  byte r, g, b;
  hsvToRgb(hue, 255, 255, &r, &g, &b);
  fill_solid(leds, NUM_LEDS, gammaRGB(r, g, b));

  hue = (hue + 2) % 1536;
}

// Pattern 1: Rotating rainbow across the strand
void rainbow(bool reset) {
  static int colorOffset = 0;
  static int totalHueSpan = 1536;
  static int increment = 4;

  if (reset) {
    colorOffset = 0;
    totalHueSpan = (1 + random(4 * ((NUM_LEDS + 31) / 32))) * 1536;
    increment = 4 + random(5);
    if (random(2) == 0) totalHueSpan = -totalHueSpan;
    if (random(2) == 0) increment = -increment;
  }

  for (int i = 0; i < NUM_LEDS; i++) {
    byte r, g, b;
    hsvToRgb(colorOffset + totalHueSpan * i / NUM_LEDS, 255, 255, &r, &g, &b);
    leds[i] = gammaRGB(r, g, b);
  }

  colorOffset += increment;
}

// Pattern 2: Single-color sine waves with dark gaps, chasing along the strand
void sineWaveChase(bool reset) {
  static int baseHue = 0;
  static int waveSpan = 720;
  static int increment = 4;
  static int waveOffset = 0;

  if (reset) {
    baseHue = random(1536);
    waveSpan = (1 + random(4 * ((NUM_LEDS + 31) / 32))) * 720;
    increment = 3 + random(4);
    if (random(2) == 0) increment = -increment;
    waveOffset = 0;
  }

  for (int i = 0; i < NUM_LEDS; i++) {
    signed char sineValue = fixSin(waveOffset + waveSpan * i / NUM_LEDS);
    byte r, g, b;
    if (sineValue >= 0) {
      // Positive half: fade toward white
      hsvToRgb(baseHue, 254 - sineValue * 2, 255, &r, &g, &b);
    } else {
      // Negative half: fade toward black (the dark gaps)
      hsvToRgb(baseHue, 255, 254 + sineValue * 2, &r, &g, &b);
    }
    leds[i] = gammaRGB(r, g, b);
  }

  waveOffset += increment;
}

// ----- Baby-friendly patterns -----
// Newborns see high-contrast black & white best, so several of these use only
// white on black. All of them move slowly.

// Pattern 3: Wide black & white stripes drifting slowly along the strand
void bwStripes(bool reset) {
  static int stripeSpan = 720 * 5;  // 720 = one white + one black stripe
  static int increment = 2;
  static int offset = 0;

  if (reset) {
    stripeSpan = 720 * (3 + random(6));  // 3-8 white stripes along the strand
    increment = 1 + random(2);
    if (random(2) == 0) increment = -increment;
    offset = 0;
  }

  for (int i = 0; i < NUM_LEDS; i++) {
    // Steepen the sine wave into stripes with soft edges
    int v = constrain(128 + fixSin(offset + stripeSpan * i / NUM_LEDS) * 4, 0, 255);
    leds[i] = gammaRGB(v, v, v);
  }

  offset += increment;
}

// Shared by Starry Night and Pastel Twinkle: lights fade in and out at random spots.
// phase 0 = off; 1..255 = rising then falling.
void twinkle(bool reset, bool pastel) {
  static uint8_t phase[NUM_LEDS];
  static uint16_t hue[NUM_LEDS];

  if (reset) memset(phase, 0, sizeof(phase));

  for (int i = 0; i < NUM_LEDS; i++) {
    if (phase[i] == 0) {
      if (random(1000) < 4) {  // Chance per frame of a new twinkle starting here
        phase[i] = 1;
        hue[i] = random(1536);
      }
    } else {
      phase[i] = (phase[i] > 253) ? 0 : phase[i] + 2;  // ~2 seconds per twinkle
    }

    byte v = (phase[i] < 128) ? phase[i] * 2 : (255 - phase[i]) * 2;
    byte r, g, b;
    if (pastel) {
      hsvToRgb(hue[i], 150, v, &r, &g, &b);
    } else {
      r = g = b = v;
    }
    leds[i] = gammaRGB(r, g, b);
  }
}

// Pattern 4: White stars twinkling on black
void starryNight(bool reset) {
  twinkle(reset, false);
}

// Pattern 5: One to three white comets with fading tails gliding on black
void whiteComet(bool reset) {
  static int numComets = 1;
  static int tailLength = 20;
  static int speed = 8;      // In 1/16ths of an LED per frame
  static long position = 0;  // In 1/16ths of an LED

  if (reset) {
    numComets = 1 + random(3);
    tailLength = 15 + random(20);
    speed = 6 + random(6);
    if (random(2) == 0) speed = -speed;
    position = 0;
  }

  int spacing = NUM_LEDS / numComets;
  int head = (int)(position / 16);
  for (int i = 0; i < NUM_LEDS; i++) {
    // Distance behind the nearest comet head (in the direction of travel)
    int d = (speed > 0) ? head - i : i - head;
    d %= spacing;
    if (d < 0) d += spacing;

    byte v = 0;
    if (d < tailLength) {
      int t = 255 * (tailLength - d) / tailLength;
      v = t * t / 255;  // Squared falloff for a smooth tail
    }
    leds[i] = gammaRGB(v, v, v);
  }

  position += speed;
  if (position < 0) position += (long)NUM_LEDS * 16;
  if (position >= (long)NUM_LEDS * 16) position -= (long)NUM_LEDS * 16;
}

// Pattern 6: Whole strand slowly breathing a soft pastel color that drifts
void breathe(bool reset) {
  static int hue = 0;
  static int phase = 0;

  if (reset) {
    hue = random(1536);
    phase = 540;  // Start at the dim point of the breath
  }

  // ~4 seconds per breath, never fully dark
  byte v = 90 + (fixSin(phase) + 127) * 165 / 254;
  byte r, g, b;
  hsvToRgb(hue, 160, v, &r, &g, &b);
  fill_solid(leds, NUM_LEDS, gammaRGB(r, g, b));

  phase = (phase + 3) % 720;
  hue = (hue + 1) % 1536;
}

// Pattern 7: Soft pastel lights twinkling on black
void pastelTwinkle(bool reset) {
  twinkle(reset, true);
}

// ===== PATTERN LIST =====
// To add a pattern: write a function like the ones above, then add it here
// and give it a name in patternNames[].
typedef void (*Pattern)(bool reset);
Pattern gPatterns[] = {
  solidColor, rainbow, sineWaveChase,
  bwStripes, starryNight, whiteComet, breathe, pastelTwinkle
};
const char* patternNames[] = {
  "Solid", "Rainbow", "Sine Chase",
  "B&W Stripes", "Starry Night", "White Comet", "Breathe", "Pastel Twinkle"
};

#define ARRAY_SIZE(A) (sizeof(A) / sizeof((A)[0]))
#define NUM_PATTERNS ARRAY_SIZE(gPatterns)

uint8_t currentPattern = 0;
bool needsReset = true;  // Next render of currentPattern starts fresh

// Cross-fade state
bool isFading = false;
uint8_t fadeFromPattern = 0;
unsigned long fadeStartTime = 0;
CRGB ledsOld[NUM_LEDS];

void nextPattern() {
  fadeFromPattern = currentPattern;
  currentPattern = (currentPattern + 1) % NUM_PATTERNS;
  needsReset = true;
  isFading = true;
  fadeStartTime = millis();
  Serial.printf("Pattern -> %d: %s\n", currentPattern, patternNames[currentPattern]);
}

void renderPattern() {
  if (isFading) {
    unsigned long elapsed = millis() - fadeStartTime;
    if (elapsed >= FADE_DURATION_MS) {
      isFading = false;
    } else {
      // Render the outgoing pattern, save it, then render the incoming one and blend
      gPatterns[fadeFromPattern](false);
      for (int i = 0; i < NUM_LEDS; i++) ledsOld[i] = leds[i];
      gPatterns[currentPattern](needsReset);
      needsReset = false;

      fract8 amount = (elapsed * 255) / FADE_DURATION_MS;
      for (int i = 0; i < NUM_LEDS; i++) {
        leds[i] = blend(ledsOld[i], leds[i], amount);
      }
      return;
    }
  }

  gPatterns[currentPattern](needsReset);
  needsReset = false;
}

// ===== DISPLAY =====
// Screen is 240x135 in landscape
void updateDisplay() {
  M5.Display.fillScreen(NAVY);
  M5.Display.setTextColor(WHITE);

  M5.Display.setTextSize(2);
  M5.Display.drawString("Jasper Lights", 10, 6);

  M5.Display.setTextColor(YELLOW);
  M5.Display.drawString(patternNames[currentPattern], 10, 34);

  // Brightness: label plus one box per level, filled up to the current level
  M5.Display.setTextColor(WHITE);
  M5.Display.drawString("Bright " + String(brightnessIndex + 1) + "/" + String(NUM_BRIGHTNESS_LEVELS), 10, 62);
  for (int i = 0; i < NUM_BRIGHTNESS_LEVELS; i++) {
    int x = 10 + i * 36;
    if (i <= brightnessIndex) M5.Display.fillRect(x, 86, 30, 14, YELLOW);
    else M5.Display.drawRect(x, 86, 30, 14, WHITE);
  }

  M5.Display.setTextSize(1);
  M5.Display.drawString("A: next pattern   B: brightness", 10, 110);
  M5.Display.drawString("v" VERSION, 10, 122);
}

void nextBrightness() {
  brightnessIndex = (brightnessIndex + 1) % NUM_BRIGHTNESS_LEVELS;
  FastLED.setBrightness(brightnessLevels[brightnessIndex]);
  Serial.printf("Brightness -> %d/%d (%d)\n", brightnessIndex + 1, NUM_BRIGHTNESS_LEVELS,
                brightnessLevels[brightnessIndex]);
}

// ===== SETUP / LOOP =====
void setup() {
  auto cfg = M5.config();
  M5.begin(cfg);
  M5.Display.setRotation(1);

  Serial.begin(115200);

  FastLED.addLeds<CHIPSET, LED_PIN, COLOR_ORDER>(leds, NUM_LEDS).setCorrection(TypicalLEDStrip);
  FastLED.setBrightness(brightnessLevels[brightnessIndex]);
  fill_solid(leds, NUM_LEDS, CRGB::Black);
  FastLED.show();

  randomSeed(esp_random());

  updateDisplay();
  Serial.println("Jasper Lights v" VERSION " ready! A: next pattern, B: brightness");
}

void loop() {
  static unsigned long lastFrameTime = 0;
  unsigned long now = millis();
  if (now - lastFrameTime < FRAME_MS) return;
  lastFrameTime = now;

  M5.update();
  if (M5.BtnA.wasPressed()) {
    nextPattern();
    updateDisplay();
  }
  if (M5.BtnB.wasPressed()) {
    nextBrightness();
    updateDisplay();
  }

  renderPattern();
  FastLED.show();
}
