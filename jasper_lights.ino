/// @file    jasper_lights.ino
/// @brief   Simple M5StickC Plus2 LED blinker for Jasper
/// @version 1.8.1
/// @date    2026-09-26
/// @author  John Cohn (patterns adapted from m5lights_v1 / Larry's patterns)
///
/// Starts in auto mode: a new pattern every minute, in shuffled order.
/// A button (big front button): next pattern, switching to manual mode (patterns
///   cross-fade); hold 2 s to go back to auto mode; hold 5 s to turn off (deep sleep),
///   press again to turn on
/// B button (side button): cycle through 6 brightness levels
/// Power button (left side): cycle through 6 speed levels
///
/// @changelog
/// v1.8.1 - Turning back on with A always starts in auto mode
/// v1.8.0 - Brightness, speed, pattern and mode saved and restored at power-up;
///          hold A 2 s = auto mode, 5 s = off (deep sleep), press A to turn on
/// v1.7.0 - Auto mode (new pattern every minute, long press A to return to it),
///          shuffled pattern order, test patterns behind INCLUDE_TEST_PATTERNS, and
///          B&W structure patterns: Cone Stripes, Pinwheel, B&W Spiral, Strings & Ring
/// v1.6.0 - Power button cycles 6 speed levels (0.4x-10x, level 2 = original speed)
/// v1.5.0 - Per-string top skip (hidden LEDs at A) so heights line up; serial 'skip R K'
/// v1.4.0 - Smoother White Comet, Falling Rings and Map Check (sub-LED positions,
///          soft leading edge); slower twinkles; serial 'pixel N' alignment check
/// v1.3.1 - Fixed strand path after the jumper (6 -> 5 -> A -> 6, not 6 -> A -> 5 -> 6);
///          Map Check pattern and serial show/run segment viewer
/// v1.3.0 - Rising Rainbow, Rainbow Spiral, Slow Orbit and Ripples patterns;
///          slower, softer Falling Rings
/// v1.2.1 - Tuned LED map built into the code
/// v1.2.0 - Structure map of the hanging ring (strings A-1..A-6, arcs 1-2..6-1),
///          serial tuner for the map, Segment Map and Falling Rings patterns
/// v1.1.1 - Starry Night and Pastel Twinkle keep separate twinkle state
/// v1.1.0 - Added baby-friendly patterns (B&W Stripes, Starry Night, White Comet,
///          Breathe, Pastel Twinkle), B button brightness, removed Wavy Flag
/// v1.0.0 - Initial version stripped down from m5lights_v1

#include <M5StickCPlus2.h>
#include <FastLED.h>
#include <Preferences.h>
#include <driver/gpio.h>
#include <esp_sleep.h>
#include <driver/rtc_io.h>

#define VERSION "1.8.1"

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

// Speed levels cycled by the power button, in 1/16ths: 0.4x, 1x, 2x, 3.5x, 6x, 10x.
// Level 2 (1x) is the speed the patterns were designed at.
const uint8_t speedLevels[] = { 6, 16, 32, 56, 96, 160 };
#define NUM_SPEED_LEVELS (sizeof(speedLevels) / sizeof(speedLevels[0]))
uint8_t speedIndex = 1;

// Scales a per-frame step by the speed setting. 'carry' keeps the leftover fraction
// between frames (one per animated variable), so slow speeds still move smoothly.
int speedStep(int step, int &carry) {
  long x = (long)step * speedLevels[speedIndex] + carry;  // In 1/16ths
  long whole = (x >= 0) ? x / 16 : -((-x + 15) / 16);     // Round down, also for negatives
  carry = x - whole * 16;
  return whole;
}

// (v + d) wrapped into 0..m-1
int wrapAdd(int v, int d, int m) {
  return ((v + d) % m + m) % m;
}

CRGB leds[NUM_LEDS];
Preferences prefs;  // Flash storage for the LED map and saved settings

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

// ===== STRUCTURE MAP =====
// The strand is hung on a hanging cone: a small ring 'A' at the top, six strings
// A-1..A-6 going down to six equally spaced points 1..6 on a wooden ring
// (~9.5" across, strings ~7"). The single strand snakes through it, covering some
// segments two or three times. Each "run" below is one pass of the strand along one
// segment, in strand order. LEDs 0-99 and 100-199 are joined by a jumper at A.
//
// These values were tuned on the real structure with the serial tuner (2026-09-26).
// To re-tune, type "tune" in the serial monitor. Tuned values are saved in
// flash and override these; "reset" goes back to these.

#define NODE_A 0  // Points on the ring are 1..6

struct Run {
  uint8_t from, to;    // Node the strand starts this run at, and ends at
  int16_t start, end;  // First and last LED index (inclusive)
  int16_t skip;        // Strings only: LEDs at the top hidden in dead space (dark).
                       // Negative leaves a gap at the top instead. The bottom LED
                       // stays at the bottom either way. Set with "skip" over serial.
};

const Run defaultRuns[] = {
  // from, to, start, end, skip
  {NODE_A, 5,   0,  13},
  {5, 4,       14,  22},
  {4, 3,       23,  29},
  {3, NODE_A,  30,  43, -2},
  {NODE_A, 4,  44,  57},
  {4, 3,       58,  65},
  {3, 2,       66,  74},
  {2, 1,       75,  84},
  {1, NODE_A,  85,  99},
  // Jumper at A
  {NODE_A, 2, 100, 115},
  {2, 1,      116, 125},
  {1, 6,      126, 133},
  {6, 5,      134, 142},
  {5, NODE_A, 143, 156},
  {NODE_A, 6, 157, 167, -3},
  {6, 1,      168, 177},
  {1, 2,      178, 189},
  {2, 3,      190, 196},
  // LEDs 197-199 are past the end of the structure
};

#define NUM_RUNS (sizeof(defaultRuns) / sizeof(defaultRuns[0]))
Run runs[NUM_RUNS];
const char* nodeNames[] = { "A", "1", "2", "3", "4", "5", "6" };

// Serial tuner state (see SERIAL TUNER below)
bool tuning = false;
int tuneRun = 0;

// Serial "show" state (see SEGMENT VIEWER below): -1 = not showing
int showSeg = -1;  // Show every run covering this segment
int showRun = -1;  // Or show just this run
int showPixel = -1;  // Or show this pixel (0 = top) on every string
#define SHOWING (showSeg >= 0 || showRun >= 0 || showPixel >= 0)

// Segments, independent of how many times the strand covers them:
//   0-5  = strings A-1..A-6
//   6-11 = ring arcs 1-2, 2-3, 3-4, 4-5, 5-6, 6-1
#define NUM_SEGMENTS 12
#define SEG_NONE 255
#define IS_STRING(seg) ((seg) < 6)

// Per-LED map, rebuilt by buildMap() whenever runs[] changes.
uint8_t ledSeg[NUM_LEDS];    // Segment, or SEG_NONE if no run covers this LED
uint8_t ledPos[NUM_LEDS];    // 0-255 along the segment: strings from A down,
                             // arcs from the lower point (6-1 from 6 to 1)
uint8_t ledDown[NUM_LEDS];   // 0 at A .. 255 at the wooden ring
uint8_t ledAngle[NUM_LEDS];  // 0-255 around the ring, point 1 = 0, increasing toward 2

// Segment for a run between two nodes; 'flip' is true when the strand runs against
// the segment's direction.
uint8_t segmentFor(uint8_t from, uint8_t to, bool &flip) {
  flip = false;
  if (from == NODE_A) return to - 1;
  if (to == NODE_A) { flip = true; return from - 1; }
  if (to == from % 6 + 1) return 6 + from - 1;
  flip = true;
  return 6 + to - 1;
}

void buildMap() {
  memset(ledSeg, SEG_NONE, sizeof(ledSeg));
  memset(ledPos, 0, sizeof(ledPos));
  memset(ledDown, 0, sizeof(ledDown));
  memset(ledAngle, 0, sizeof(ledAngle));

  for (int r = 0; r < NUM_RUNS; r++) {
    bool flip;
    uint8_t seg = segmentFor(runs[r].from, runs[r].to, flip);
    int n = runs[r].end - runs[r].start + 1;
    int skip = IS_STRING(seg) ? runs[r].skip : 0;
    int slots = n - skip;  // Evenly spaced positions from the segment's start to its end
    if (slots < 1) continue;
    for (int i = 0; i < n; i++) {
      int led = runs[r].start + i;
      if (led < 0 || led >= NUM_LEDS) continue;
      int p = flip ? n - 1 - i : i;  // Index from the segment's start (top of a string)
      int q = p - skip;
      if (q < 0) continue;           // Hidden at the top: left unmapped (dark)
      uint8_t pos = (2 * q + 1) * 256 / (2 * slots);  // Center of each LED's share
      ledSeg[led] = seg;
      ledPos[led] = pos;
      if (IS_STRING(seg)) {
        ledDown[led] = pos;
        ledAngle[led] = seg * 256 / 6;
      } else {
        ledDown[led] = 255;
        ledAngle[led] = (seg - 6) * 256 / 6 + pos / 6;
      }
    }
  }
}

// ===== PATTERNS =====
// Each pattern draws one frame into leds[]. 'reset' is true on the first frame
// after the pattern is selected, so it can pick fresh random parameters.

// Pattern: Solid color slowly fading through the rainbow
void solidColor(bool reset) {
  static int hue = 0;
  static int carry = 0;
  if (reset) hue = random(1536);

  byte r, g, b;
  hsvToRgb(hue, 255, 255, &r, &g, &b);
  fill_solid(leds, NUM_LEDS, gammaRGB(r, g, b));

  hue = wrapAdd(hue, speedStep(2, carry), 1536);
}

// Pattern: Rotating rainbow across the strand
void rainbow(bool reset) {
  static int colorOffset = 0;
  static int totalHueSpan = 1536;
  static int increment = 4;
  static int carry = 0;

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

  colorOffset = wrapAdd(colorOffset, speedStep(increment, carry), 1536);
}

// Pattern: Single-color sine waves with dark gaps, chasing along the strand
void sineWaveChase(bool reset) {
  static int baseHue = 0;
  static int waveSpan = 720;
  static int increment = 4;
  static int waveOffset = 0;
  static int carry = 0;

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

  waveOffset = wrapAdd(waveOffset, speedStep(increment, carry), 720);
}

// ----- Baby-friendly patterns -----
// Newborns see high-contrast black & white best, so several of these use only
// white on black. All of them move slowly.

// Pattern: Wide black & white stripes drifting slowly along the strand
void bwStripes(bool reset) {
  static int stripeSpan = 720 * 5;  // 720 = one white + one black stripe
  static int increment = 2;
  static int offset = 0;
  static int carry = 0;

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

  offset = wrapAdd(offset, speedStep(increment, carry), 720);
}

// Used by Starry Night and Pastel Twinkle: lights fade in and out at random spots.
// Each pattern owns its own TwinkleState so they don't disturb each other when
// both are drawn during a cross-fade.
// phase 0 = off; 1..255 = rising then falling.
struct TwinkleState {
  uint8_t phase[NUM_LEDS];
  uint16_t hue[NUM_LEDS];
  int carry;
};

// Explicit prototype so the Arduino builder doesn't auto-generate one above the struct
void twinkle(TwinkleState &state, bool reset, bool pastel);

void twinkle(TwinkleState &state, bool reset, bool pastel) {
  uint8_t *phase = state.phase;
  uint16_t *hue = state.hue;

  if (reset) memset(phase, 0, sizeof(state.phase));
  int step = speedStep(1, state.carry);  // ~4 seconds per twinkle at 1x

  for (int i = 0; i < NUM_LEDS; i++) {
    if (phase[i] == 0) {
      // Chance per frame of a new twinkle starting here (2 in 1000 at 1x)
      if (random(16000) < 2 * speedLevels[speedIndex]) {
        phase[i] = 1;
        hue[i] = random(1536);
      }
    } else if (phase[i] + step > 255) {
      phase[i] = 0;
    } else {
      phase[i] += step;
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

// Pattern: White stars twinkling on black
void starryNight(bool reset) {
  static TwinkleState state;
  twinkle(state, reset, false);
}

// Pattern: One to three white comets with fading tails gliding on black
void whiteComet(bool reset) {
  static int numComets = 1;
  static int tailLength = 20;
  static int speed = 8;      // In 1/16ths of an LED per frame
  static long position = 0;  // In 1/16ths of an LED
  static int carry = 0;

  if (reset) {
    numComets = 1 + random(3);
    tailLength = 15 + random(20);
    speed = 6 + random(6);
    if (random(2) == 0) speed = -speed;
    position = 0;
  }

  // Everything in 1/16ths of an LED, so the comet moves smoothly between LEDs
  long spacing16 = (long)(NUM_LEDS / numComets) * 16;
  long tail16 = (long)tailLength * 16;
  for (int i = 0; i < NUM_LEDS; i++) {
    // Distance behind the nearest comet head (in the direction of travel)
    long d = (speed > 0) ? position - i * 16L : i * 16L - position;
    d %= spacing16;
    if (d < 0) d += spacing16;
    if (d > spacing16 - 16) d -= spacing16;  // Within one LED ahead of a head

    long t = 0;
    if (d < 0) t = 255 * (16 + d) / 16;                      // Fading in just ahead
    else if (d < tail16) t = 255 * (tail16 - d) / tail16;    // Tail
    byte v = t * t / 255;  // Squared falloff for a smooth tail
    leds[i] = gammaRGB(v, v, v);
  }

  position += speedStep(speed, carry);
  if (position < 0) position += (long)NUM_LEDS * 16;
  if (position >= (long)NUM_LEDS * 16) position -= (long)NUM_LEDS * 16;
}

// Pattern: Whole strand slowly breathing a soft pastel color that drifts
void breathe(bool reset) {
  static int hue = 0;
  static int phase = 0;
  static int carryPhase = 0, carryHue = 0;

  if (reset) {
    hue = random(1536);
    phase = 540;  // Start at the dim point of the breath
  }

  // ~4 seconds per breath, never fully dark
  byte v = 90 + (fixSin(phase) + 127) * 165 / 254;
  byte r, g, b;
  hsvToRgb(hue, 160, v, &r, &g, &b);
  fill_solid(leds, NUM_LEDS, gammaRGB(r, g, b));

  phase = wrapAdd(phase, speedStep(3, carryPhase), 720);
  hue = wrapAdd(hue, speedStep(1, carryHue), 1536);
}

// Pattern: Soft pastel lights twinkling on black
void pastelTwinkle(bool reset) {
  static TwinkleState state;
  twinkle(state, reset, true);
}

// ----- Structure patterns -----
// These use the structure map above.

// Fixed color for each segment, used by Segment Map and the tuner.
// Strings A-1..A-6 get red, yellow, green, cyan, blue, magenta; each ring arc gets
// the color halfway between its two strings (1-2 orange, 2-3 lime, ... 6-1 pink).
const char* segmentNames[NUM_SEGMENTS] = {
  "A-1", "A-2", "A-3", "A-4", "A-5", "A-6", "1-2", "2-3", "3-4", "4-5", "5-6", "6-1"
};
const char* segmentColorNames[NUM_SEGMENTS] = {
  "red", "yellow", "green", "cyan", "blue", "magenta",
  "orange", "lime", "sea green", "azure", "violet", "pink"
};

CRGB segmentColor(uint8_t seg, byte v) {
  int hue = IS_STRING(seg) ? seg * 256 : (seg - 6) * 256 + 128;
  byte r, g, b;
  hsvToRgb(hue, 255, v, &r, &g, &b);
  return gammaRGB(r, g, b);
}

// Test pattern: Each string and ring arc in its own fixed color (see segmentColor()).
// Useful for checking the map: every copy of a doubled segment should match.
void segmentMap(bool reset) {
  for (int i = 0; i < NUM_LEDS; i++) {
    leds[i] = (ledSeg[i] == SEG_NONE) ? CRGB::Black : segmentColor(ledSeg[i], 255);
  }
}

// Pattern: A ring of light slides down all six strings at once, lands on the
// wooden ring and fades out. Sometimes it rises from the ring up to A instead.
void fallingRings(bool reset) {
  static int pos = 0;      // Leading edge, in 1/16ths of ledDown units
  static int speed = 8;
  static int hue = 0;
  static bool rising = false;
  static int carry = 0;
  const int tail = 90;     // Length of the glow behind the leading edge
  const int lead = 24;     // Soft fade-in ahead of the leading edge (~1.3 LEDs)

  if (reset) {
    pos = -lead * 16;
    speed = 6 + random(6);  // ~7-11 s from A to the ring
    hue = random(1536);
    rising = random(3) == 0;
  }

  for (int i = 0; i < NUM_LEDS; i++) {
    if (ledSeg[i] == SEG_NONE) { leds[i] = CRGB::Black; continue; }
    int down = rising ? 255 - ledDown[i] : ledDown[i];
    long d = pos - down * 16L;  // How far the leading edge is past this LED, in 1/16ths
    long t = 0;
    if (d < 0 && d > -lead * 16) t = 255 * (lead * 16L + d) / (lead * 16);  // Fading in
    else if (d >= 0 && d < tail * 16L) t = 255 * (tail * 16L - d) / (tail * 16);
    byte v = t * t / 255;
    byte r, g, b;
    hsvToRgb(hue, 220, v, &r, &g, &b);
    leds[i] = gammaRGB(r, g, b);
  }

  pos += speedStep(speed, carry);
  if (pos / 16 > 255 + tail + 20) {  // Fully faded, plus a short dark pause
    pos = -lead * 16;
    hue = (hue + 256 + random(512)) % 1536;
  }
}

// Pattern: Rainbow from A down to the ring, drifting slowly up all strings together
void risingRainbow(bool reset) {
  static long offset = 0;   // In 1/16ths of a hue unit
  static int span = 768;    // Hue change from A to the ring
  static int carry = 0;

  if (reset) {
    offset = random(1536) * 16L;
    span = 512 + random(513);  // A third to two thirds of the rainbow
  }

  int base = offset / 16;
  for (int i = 0; i < NUM_LEDS; i++) {
    if (ledSeg[i] == SEG_NONE) { leds[i] = CRGB::Black; continue; }
    // Hue grows with distance down, and the base grows over time, so each
    // color moves up
    byte r, g, b;
    hsvToRgb(base + ledDown[i] * span / 256, 230, 255, &r, &g, &b);
    leds[i] = gammaRGB(r, g, b);
  }

  offset = wrapAdd(offset, speedStep(8, carry), 1536 * 16);  // ~50 s per rainbow cycle at 1x
}

// Pattern: Rainbow around the ring, twisting up the strings, slowly turning
void rainbowSpiral(bool reset) {
  static long offset = 0;   // In 1/16ths of a hue unit
  static int twist = 512;   // Extra hue change from A to the ring
  static int carry = 0;

  if (reset) {
    offset = random(1536) * 16L;
    twist = 256 + random(513);
    if (random(2) == 0) twist = -twist;
  }

  int base = offset / 16;
  for (int i = 0; i < NUM_LEDS; i++) {
    if (ledSeg[i] == SEG_NONE) { leds[i] = CRGB::Black; continue; }
    byte r, g, b;
    hsvToRgb(base + ledAngle[i] * 6 + ledDown[i] * twist / 256, 230, 255, &r, &g, &b);
    leds[i] = gammaRGB(r, g, b);
  }

  offset = wrapAdd(offset, speedStep(6, carry), 1536 * 16);  // ~70 s per turn at 1x
}

// Pattern: One or two soft pastel glows slowly circling the structure. Each
// string lights up as a glow passes it, with the ring glowing underneath.
void slowOrbit(bool reset) {
  static long angle = 0;   // In 1/16ths of a ledAngle unit
  static int speed = 3;
  static int glows = 1;
  static int hue = 0;
  static int carryAngle = 0, carryHue = 0;

  if (reset) {
    angle = random(256) * 16L;
    speed = random(2) ? 3 : -3;  // ~25 s per lap
    glows = 1 + random(2);
    hue = random(1536);
  }

  int period = 256 / glows;
  int width = period * 2 / 5;
  int a = angle / 16;
  for (int i = 0; i < NUM_LEDS; i++) {
    if (ledSeg[i] == SEG_NONE) { leds[i] = CRGB::Black; continue; }
    // Angular distance to the nearest glow
    int d = ((ledAngle[i] - a) % period + period) % period;
    d = min(d, period - d);
    byte v = 0;
    if (d < width) {
      int t = 255 * (width - d) / width;
      v = t * t / 255;
    }
    // With two glows, the second one is the complementary color
    int glowIndex = ((ledAngle[i] - a + period / 2) % 256 + 256) % 256 / period;
    byte r, g, b;
    hsvToRgb(hue + glowIndex * 768, 170, v, &r, &g, &b);
    leds[i] = gammaRGB(r, g, b);
  }

  angle = wrapAdd(angle, speedStep(speed, carryAngle), 256 * 16);
  hue = wrapAdd(hue, speedStep(1, carryHue), 1536);
}

// Pattern: Soft pastel waves drifting down the strings; the ring glows as each
// wave arrives
void ripples(bool reset) {
  static int phase = 0;
  static int hue = 0;
  static int carryPhase = 0, carryHue = 0;

  if (reset) {
    phase = random(720);
    hue = random(1536);
  }

  for (int i = 0; i < NUM_LEDS; i++) {
    if (ledSeg[i] == SEG_NONE) { leds[i] = CRGB::Black; continue; }
    // 1.5 waves along a string; squared for darker gaps between waves
    int s = fixSin(phase + ledDown[i] * 1080 / 256) + 127;  // 0..254
    byte v = s * s / 254;
    byte r, g, b;
    hsvToRgb(hue + ledDown[i] * 2, 170, v, &r, &g, &b);
    leds[i] = gammaRGB(r, g, b);
  }

  // Stepping backward makes the waves move down, ~12 s each at 1x
  phase = wrapAdd(phase, speedStep(-1, carryPhase), 720);
  hue = wrapAdd(hue, speedStep(1, carryHue), 1536);
}

// ----- Black & white structure patterns -----
// High contrast for a baby, all slow at speed level 2.

// Softened square wave: -127..127 sine in, steep but not hard-edged 0..255 out
byte softSquare(int sine, int steepness) {
  return constrain(128 + sine * steepness, 0, 255);
}

// Pattern: White bands drifting down (or up) all strings together; the ring
// glows as each band arrives
void coneStripes(bool reset) {
  static int offset = 0;
  static int increment = -1;
  static int bands = 1;  // Bands per string length
  static int carry = 0;

  if (reset) {
    offset = random(720);
    bands = 1 + random(2);
    increment = random(3) ? -1 : 1;  // Usually down (~12 s per band at 1x)
  }

  for (int i = 0; i < NUM_LEDS; i++) {
    if (ledSeg[i] == SEG_NONE) { leds[i] = CRGB::Black; continue; }
    byte v = softSquare(fixSin(offset + ledDown[i] * bands * 720 / 256), 3);
    leds[i] = gammaRGB(v, v, v);
  }

  offset = wrapAdd(offset, speedStep(increment, carry), 720);
}

// Shared by Pinwheel and B&W Spiral: white and black wedges around the structure,
// optionally twisting down the strings, slowly turning. Each pattern owns its own
// WedgeState so they don't disturb each other during a cross-fade.
struct WedgeState {
  int angle;    // In 1/16ths of a ledAngle unit
  int speed;
  int wedges;   // White wedges around the ring
  int twist;    // Phase change from A to the ring (720 = one full stripe)
  int carry;
};

// Explicit prototype so the Arduino builder doesn't auto-generate one above the struct
void wedges(WedgeState &state, bool reset, bool twisted);

void wedges(WedgeState &state, bool reset, bool twisted) {
  if (reset) {
    state.angle = random(256) * 16;
    state.speed = random(2) ? 2 : -2;  // ~6 s for white to move one string over at 1x
    state.wedges = twisted ? 1 + random(2) : 3;  // Pinwheel: every other string white
    state.twist = twisted ? (random(2) ? 720 : -720) : 0;
  }

  int a = state.angle / 16;
  for (int i = 0; i < NUM_LEDS; i++) {
    if (ledSeg[i] == SEG_NONE) { leds[i] = CRGB::Black; continue; }
    int phase = (ledAngle[i] - a) * state.wedges * 720 / 256 + ledDown[i] * state.twist / 256;
    byte v = softSquare(fixSin(phase), 3);
    leds[i] = gammaRGB(v, v, v);
  }

  state.angle = wrapAdd(state.angle, speedStep(state.speed, state.carry), 256 * 16);
}

// Pattern: Every other string white, the white slowly rotating around
void pinwheel(bool reset) {
  static WedgeState state;
  wedges(state, reset, false);
}

// Pattern: White and black stripes twisting down the strings like a barber pole,
// slowly turning
void bwSpiral(bool reset) {
  static WedgeState state;
  wedges(state, reset, true);
}

// Pattern: Strings and ring slowly trade places: strings white while the ring is
// dark, then the other way round
void stringsAndRing(bool reset) {
  static int phase = 0;
  static int carry = 0;
  if (reset) phase = 540;  // Start with the strings dim

  // ~12 s per full swap cycle at 1x
  int s = fixSin(phase);
  for (int i = 0; i < NUM_LEDS; i++) {
    if (ledSeg[i] == SEG_NONE) { leds[i] = CRGB::Black; continue; }
    byte v = softSquare(IS_STRING(ledSeg[i]) ? s : -s, 1);
    leds[i] = gammaRGB(v, v, v);
  }

  phase = wrapAdd(phase, speedStep(1, carry), 720);
}

// Test pattern: Diagnostic for the map. Repeats every 20 s:
//   0-6 s:  whole structure pure red, then green, then blue (2 s each). Every
//           LED should match; if some don't, those LEDs use a different color order.
//   6-20 s: a white dot on every string moving from A down to the ring; the ring
//           lights when they arrive. All dots, including both copies of A-5,
//           should move together.
void mapCheck(bool reset) {
  static unsigned long startTime = 0;
  if (reset) startTime = millis();
  unsigned long t = (millis() - startTime) % 20000;

  if (t < 6000) {
    CRGB c = (t < 2000) ? CRGB::Red : (t < 4000) ? CRGB::Green : CRGB::Blue;
    for (int i = 0; i < NUM_LEDS; i++) leds[i] = (ledSeg[i] == SEG_NONE) ? CRGB::Black : c;
    return;
  }

  // In 1/16ths of ledDown units: 0..279, past 255 so the ring stays lit a moment
  long dot = (long)(t - 6000) * 280 * 16 / 14000;
  const long width = 20 * 16;
  for (int i = 0; i < NUM_LEDS; i++) {
    long v = 0;
    if (ledSeg[i] != SEG_NONE) {
      if (IS_STRING(ledSeg[i])) {
        long dist = abs(ledDown[i] * 16L - dot);
        if (dist < width) v = 255 * (width - dist) / width;
      } else {
        v = constrain((dot - 230 * 16L) * 255 / (25 * 16), 0, 255);  // Fades in as dots arrive
      }
    }
    leds[i] = gammaRGB(v, v, v);
  }
}

// ===== PATTERN LIST =====
// To add a pattern: write a function like the ones above, then add it here
// and give it a name in patternNames[]. Patterns play in a shuffled order.

// Set to 1 to include the map test patterns (Segment Map, Map Check) in the rotation
#define INCLUDE_TEST_PATTERNS 0

typedef void (*Pattern)(bool reset);
Pattern gPatterns[] = {
  solidColor, rainbow, sineWaveChase,
  bwStripes, starryNight, whiteComet, breathe, pastelTwinkle,
  fallingRings, risingRainbow, rainbowSpiral, slowOrbit, ripples,
  coneStripes, pinwheel, bwSpiral, stringsAndRing,
#if INCLUDE_TEST_PATTERNS
  segmentMap, mapCheck,
#endif
};
const char* patternNames[] = {
  "Solid", "Rainbow", "Sine Chase",
  "B&W Stripes", "Starry Night", "White Comet", "Breathe", "Pastel Twinkle",
  "Falling Rings", "Rising Rainbow", "Rainbow Spiral", "Slow Orbit", "Ripples",
  "Cone Stripes", "Pinwheel", "B&W Spiral", "Strings & Ring",
#if INCLUDE_TEST_PATTERNS
  "Segment Map", "Map Check",
#endif
};

#define ARRAY_SIZE(A) (sizeof(A) / sizeof((A)[0]))
#define NUM_PATTERNS ARRAY_SIZE(gPatterns)

uint8_t currentPattern = 0;
bool needsReset = true;  // Next render of currentPattern starts fresh

// Auto mode: move to the next pattern every AUTO_PATTERN_MS. A short press of A
// switches to manual (A steps through patterns); a long press goes back to auto.
#define AUTO_PATTERN_MS 60000
#define AUTO_PRESS_MS 2000  // Hold A this long (and release) to go back to auto mode
#define OFF_PRESS_MS 5000   // Hold A this long to turn off; press A again to turn on
bool autoMode = true;
unsigned long patternStartTime = 0;

// Shuffled play order; reshuffled each time through, never repeating a pattern
// back to back
uint8_t playOrder[NUM_PATTERNS];
uint8_t playPos = 0;

void shufflePlayOrder() {
  for (int i = 0; i < NUM_PATTERNS; i++) playOrder[i] = i;
  for (int i = NUM_PATTERNS - 1; i > 0; i--) {
    int j = random(i + 1);
    uint8_t t = playOrder[i]; playOrder[i] = playOrder[j]; playOrder[j] = t;
  }
  if (NUM_PATTERNS > 1 && playOrder[0] == currentPattern) {
    uint8_t t = playOrder[0]; playOrder[0] = playOrder[1]; playOrder[1] = t;
  }
  playPos = 0;
}

// Cross-fade state
bool isFading = false;
uint8_t fadeFromPattern = 0;
unsigned long fadeStartTime = 0;
CRGB ledsOld[NUM_LEDS];

void nextPattern() {
  fadeFromPattern = currentPattern;
  if (++playPos >= NUM_PATTERNS) shufflePlayOrder();
  currentPattern = playOrder[playPos];
  patternStartTime = millis();
  saveSettings();
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

// ===== SAVED SETTINGS =====
// Brightness, speed, pattern and auto/manual mode are saved in flash whenever they
// change, and restored at power-up. The pattern is saved by name, so adding or
// reordering patterns doesn't restore the wrong one.
void saveSettings() {
  prefs.begin("jlstate", false);
  prefs.putUChar("bright", brightnessIndex);
  prefs.putUChar("speed", speedIndex);
  prefs.putBool("auto", autoMode);
  prefs.putString("pattern", patternNames[currentPattern]);
  prefs.end();
}

void loadSettings() {
  prefs.begin("jlstate", true);
  brightnessIndex = min((int)prefs.getUChar("bright", brightnessIndex), (int)NUM_BRIGHTNESS_LEVELS - 1);
  speedIndex = min((int)prefs.getUChar("speed", speedIndex), (int)NUM_SPEED_LEVELS - 1);
  autoMode = prefs.getBool("auto", true);
  String name = prefs.getString("pattern", "");
  prefs.end();

  // Start the shuffled order with the saved pattern
  for (int i = 0; i < NUM_PATTERNS; i++) {
    if (name == patternNames[playOrder[i]]) {
      uint8_t t = playOrder[0]; playOrder[0] = playOrder[i]; playOrder[i] = t;
      break;
    }
  }
  currentPattern = playOrder[0];
}

// ===== OFF (DEEP SLEEP) =====
// Holding A for 5 s turns everything off: LEDs dark, screen off, and the ESP32 in
// deep sleep, drawing almost nothing. Pressing A wakes it, which restarts the sketch
// and restores the saved settings. The power-hold pin is kept high during sleep, or
// on battery the M5 would lose power entirely and need the power button to restart.
#define BTN_A_PIN GPIO_NUM_37
#define POWER_HOLD_PIN GPIO_NUM_4

// Waits (up to 3 s) for A to be released
void waitForRelease() {
  unsigned long start = millis();
  while (digitalRead(BTN_A_PIN) == LOW && millis() - start < 3000) delay(10);
  delay(50);
}

void turnOff() {
  saveSettings();
  fill_solid(leds, NUM_LEDS, CRGB::Black);
  FastLED.show();
  M5.Display.setBrightness(0);
  M5.Display.sleep();
  Serial.println("Off. Press A to turn back on.");
  Serial.flush();

  // Wait for A to be released, or it would wake straight away
  waitForRelease();

  // Keep the LED data line low and the power held on through deep sleep
  pinMode(LED_PIN, OUTPUT);
  digitalWrite(LED_PIN, LOW);
  gpio_hold_en((gpio_num_t)LED_PIN);
  gpio_hold_en(POWER_HOLD_PIN);
  gpio_deep_sleep_hold_en();

  esp_sleep_enable_ext0_wakeup(BTN_A_PIN, 0);  // Wake when A goes low (pressed)
  esp_deep_sleep_start();
}

// Shown while A is held long enough to do something on release
void showHoldScreen(bool off) {
  M5.Display.fillScreen(off ? BLACK : DARKGREY);
  M5.Display.setTextColor(WHITE);
  M5.Display.setTextSize(2);
  if (off) {
    M5.Display.drawString("Release to", 10, 40);
    M5.Display.drawString("turn OFF", 10, 66);
  } else {
    M5.Display.drawString("Release: AUTO", 10, 40);
    M5.Display.drawString("Keep holding:", 10, 74);
    M5.Display.drawString("OFF", 10, 98);
  }
}

// ===== DISPLAY =====
// Screen is 240x135 in landscape
void updateDisplay() {
  if (!tuning && SHOWING) {
    M5.Display.fillScreen(DARKGREEN);
    M5.Display.setTextColor(WHITE);
    M5.Display.setTextSize(2);
    M5.Display.drawString("SHOWING", 10, 6);
    M5.Display.setTextColor(YELLOW);
    if (showSeg >= 0) M5.Display.drawString(String("Segment ") + segmentNames[showSeg], 10, 34);
    else if (showRun >= 0) M5.Display.drawString("Run " + String(showRun + 1), 10, 34);
    else M5.Display.drawString("Pixel " + String(showPixel) + " from top", 10, 34);
    M5.Display.setTextColor(WHITE);
    M5.Display.setTextSize(1);
    if (showPixel < 0) M5.Display.drawString("Green = start, red = end", 10, 70);
    M5.Display.drawString("A: back to patterns", 10, 110);
    return;
  }
  if (tuning) {
    bool flip;
    uint8_t seg = segmentFor(runs[tuneRun].from, runs[tuneRun].to, flip);
    M5.Display.fillScreen(MAROON);
    M5.Display.setTextColor(WHITE);
    M5.Display.setTextSize(2);
    M5.Display.drawString("TUNING " + String(tuneRun + 1) + "/" + String(NUM_RUNS), 10, 6);
    M5.Display.setTextColor(YELLOW);
    M5.Display.drawString(String(nodeNames[runs[tuneRun].from]) + " -> " + nodeNames[runs[tuneRun].to] +
                          "  (" + segmentNames[seg] + ")", 10, 34);
    M5.Display.setTextColor(WHITE);
    M5.Display.drawString("LEDs " + String(runs[tuneRun].start) + " - " + String(runs[tuneRun].end), 10, 62);
    M5.Display.setTextSize(1);
    M5.Display.drawString("Use serial monitor. A: accept", 10, 110);
    return;
  }

  M5.Display.fillScreen(NAVY);
  M5.Display.setTextColor(WHITE);

  M5.Display.setTextSize(2);
  M5.Display.drawString("Jasper Lights", 10, 6);
  M5.Display.setTextColor(autoMode ? GREEN : ORANGE);
  M5.Display.drawString(autoMode ? "AUTO" : "MAN", autoMode ? 182 : 194, 6);

  M5.Display.setTextColor(YELLOW);
  M5.Display.drawString(patternNames[currentPattern], 10, 34);

  // Brightness and speed: label plus one box per level, filled up to the current level
  M5.Display.setTextColor(WHITE);
  M5.Display.drawString("Bright", 10, 60);
  M5.Display.drawString("Speed", 10, 84);
  for (int i = 0; i < 6; i++) {
    int x = 96 + i * 23;
    if (i <= brightnessIndex) M5.Display.fillRect(x, 60, 19, 14, YELLOW);
    else M5.Display.drawRect(x, 60, 19, 14, WHITE);
    if (i <= speedIndex) M5.Display.fillRect(x, 84, 19, 14, CYAN);
    else M5.Display.drawRect(x, 84, 19, 14, WHITE);
  }

  M5.Display.setTextSize(1);
  M5.Display.drawString("A:next 2s=auto 5s=off B:bri PWR:spd", 10, 110);
  M5.Display.drawString("v" VERSION " by zatar", 10, 122);
}

void setSpeed(int index) {
  speedIndex = index;
  saveSettings();
  Serial.printf("Speed -> %d/%d (%d.%02dx)\n", speedIndex + 1, NUM_SPEED_LEVELS,
                speedLevels[speedIndex] / 16, speedLevels[speedIndex] % 16 * 100 / 16);
}

void nextSpeed() {
  setSpeed((speedIndex + 1) % NUM_SPEED_LEVELS);
}

void nextBrightness() {
  brightnessIndex = (brightnessIndex + 1) % NUM_BRIGHTNESS_LEVELS;
  FastLED.setBrightness(brightnessLevels[brightnessIndex]);
  saveSettings();
  Serial.printf("Brightness -> %d/%d (%d)\n", brightnessIndex + 1, NUM_BRIGHTNESS_LEVELS,
                brightnessLevels[brightnessIndex]);
}

// ===== MAP STORAGE =====
// Only the start/end of each run are saved, so a change to the strand path in
// defaultRuns[] (different number of runs) makes old saved values be ignored.

void loadRuns() {
  memcpy(runs, defaultRuns, sizeof(runs));
  int16_t saved[NUM_RUNS * 3];  // start, end, skip per run
  prefs.begin("jlmap", true);
  size_t len = prefs.getBytesLength("runs");
  int fields = (len == sizeof(saved)) ? 3 : (len == NUM_RUNS * 2 * sizeof(int16_t)) ? 2 : 0;
  if (fields) {  // Older saves have start and end only
    prefs.getBytes("runs", saved, len);
    for (int r = 0; r < NUM_RUNS; r++) {
      runs[r].start = saved[r * fields];
      runs[r].end = saved[r * fields + 1];
      if (fields == 3) runs[r].skip = saved[r * fields + 2];
    }
    Serial.println("Loaded tuned map from flash");
  }
  prefs.end();
  buildMap();
}

void saveRuns() {
  int16_t saved[NUM_RUNS * 3];
  for (int r = 0; r < NUM_RUNS; r++) {
    saved[r * 3] = runs[r].start;
    saved[r * 3 + 1] = runs[r].end;
    saved[r * 3 + 2] = runs[r].skip;
  }
  prefs.begin("jlmap", false);
  prefs.putBytes("runs", saved, sizeof(saved));
  prefs.end();
  Serial.println("Map saved to flash");
}

void resetRuns() {
  prefs.begin("jlmap", false);
  prefs.clear();
  prefs.end();
  memcpy(runs, defaultRuns, sizeof(runs));
  buildMap();
  Serial.println("Map reset to defaults");
}

// Prints the map as C code, ready to paste over defaultRuns[]
void printRuns() {
  Serial.println("\nconst Run defaultRuns[] = {");
  for (int r = 0; r < NUM_RUNS; r++) {
    if (r > 0 && runs[r].from == NODE_A && runs[r - 1].to == NODE_A &&
        runs[r].start != runs[r - 1].end + 1) {
      Serial.println("  // Gap");
    }
    Serial.printf("  {%s, %s, %3d, %3d, %d},  // %s -> %s, %d LEDs\n",
                  runs[r].from == NODE_A ? "NODE_A" : nodeNames[runs[r].from],
                  runs[r].to == NODE_A ? "NODE_A" : nodeNames[runs[r].to],
                  runs[r].start, runs[r].end, runs[r].skip,
                  nodeNames[runs[r].from], nodeNames[runs[r].to],
                  runs[r].end - runs[r].start + 1);
  }
  Serial.println("};");
}

// ===== SERIAL TUNER =====
// Type "tune" in the serial monitor (115200 baud, line ending on). Steps through each
// run in strand order: the run being tuned is white with its first LED green and
// last LED red; everything else shows dimly in its Segment Map color. Changing a
// run's end also moves the next run's start (and vice versa) if they were touching.
void printTuneRun() {
  bool flip;
  uint8_t seg = segmentFor(runs[tuneRun].from, runs[tuneRun].to, flip);
  Serial.printf("\nRun %d/%d: %s -> %s   (segment %s, %s in Segment Map)\n",
                tuneRun + 1, NUM_RUNS, nodeNames[runs[tuneRun].from], nodeNames[runs[tuneRun].to],
                segmentNames[seg], segmentColorNames[seg]);
  Serial.printf("  start %d, end %d (%d LEDs)\n", runs[tuneRun].start, runs[tuneRun].end,
                runs[tuneRun].end - runs[tuneRun].start + 1);
  Serial.printf("  Green should be the first LED after %s, red the last LED before %s.\n",
                nodeNames[runs[tuneRun].from], nodeNames[runs[tuneRun].to]);
  Serial.println("  Enter/y = accept, s N = set start, e N = set end, N M = set both,");
  Serial.println("  b = back, q = finish");
}

void startTuning() {
  showSeg = showRun = showPixel = -1;
  tuning = true;
  tuneRun = 0;
  isFading = false;
  printTuneRun();
}

void finishTuning() {
  tuning = false;
  needsReset = true;
  saveRuns();
  printRuns();
  Serial.println("Tuning done. Paste the table above over defaultRuns[] to make it permanent.");
}

// Returns false (and changes nothing) if the new value would make a run invalid
bool setRunStart(int r, int v) {
  int prev = r - 1;
  bool linked = prev >= 0 && runs[prev].end == runs[r].start - 1;
  if (v < 0 || v > runs[r].end) return false;
  if (linked && v - 1 < runs[prev].start) return false;
  runs[r].start = v;
  if (linked) runs[prev].end = v - 1;
  return true;
}

bool setRunEnd(int r, int v) {
  int next = r + 1;
  bool linked = next < NUM_RUNS && runs[next].start == runs[r].end + 1;
  if (v >= NUM_LEDS || v < runs[r].start) return false;
  if (linked && v + 1 > runs[next].end) return false;
  runs[r].end = v;
  if (linked) runs[next].start = v + 1;
  return true;
}

void renderTune() {
  for (int i = 0; i < NUM_LEDS; i++) {
    leds[i] = (ledSeg[i] == SEG_NONE) ? CRGB::Black : segmentColor(ledSeg[i], 130);
  }
  for (int i = runs[tuneRun].start; i <= runs[tuneRun].end; i++) leds[i] = CRGB::White;
  leds[runs[tuneRun].start] = CRGB::Green;
  leds[runs[tuneRun].end] = CRGB::Red;
}

// ===== SEGMENT VIEWER =====
// "show A-5" lights every run covering that segment; "run 14" lights one run.
// Lit runs are white with green at the segment's start and red at its end, as the
// map understands them: strings start at the top (A); ring arcs start at the
// lower-numbered point (6-1 starts at 6). A run with green in the wrong place is
// mapped backwards. "off" or the A button goes back to the patterns.

// Parses "A-5", "5-a", "a5", "1-2", "2 1"... into a segment; returns SEG_NONE if invalid
uint8_t parseSegment(const char *str) {
  int nodes[2], n = 0;
  for (const char *c = str; *c && n < 2; c++) {
    if (*c == 'a' || *c == 'A') nodes[n++] = NODE_A;
    else if (*c >= '1' && *c <= '6') nodes[n++] = *c - '0';
    else if (*c != '-' && *c != ' ') return SEG_NONE;
  }
  if (n != 2 || nodes[0] == nodes[1]) return SEG_NONE;
  if (nodes[0] != NODE_A && nodes[1] != NODE_A) {
    // Ring points must be neighbors
    if (nodes[1] != nodes[0] % 6 + 1 && nodes[0] != nodes[1] % 6 + 1) return SEG_NONE;
  }
  bool flip;
  return segmentFor(nodes[0], nodes[1], flip);
}

bool runIsShown(int r) {
  if (showRun >= 0) return r == showRun;
  bool flip;
  return showSeg >= 0 && segmentFor(runs[r].from, runs[r].to, flip) == showSeg;
}

void printShownRun(int r) {
  bool flip;
  uint8_t seg = segmentFor(runs[r].from, runs[r].to, flip);
  // Start/end LEDs in segment order (green/red)
  int first = flip ? runs[r].end : runs[r].start;
  int last = flip ? runs[r].start : runs[r].end;
  Serial.printf("  Run %d: %s -> %s, LEDs %d-%d (%d). Segment %s: green = LED %d, red = LED %d\n",
                r + 1, nodeNames[runs[r].from], nodeNames[runs[r].to],
                runs[r].start, runs[r].end, runs[r].end - runs[r].start + 1,
                segmentNames[seg], first, last);
}

void startShow(int seg, int run) {
  showSeg = seg;
  showRun = run;
  showPixel = -1;
  isFading = false;
  if (seg >= 0) {
    Serial.printf("\nShowing segment %s (%s). Green = %s, red = %s\n", segmentNames[seg],
                  IS_STRING(seg) ? "string" : "ring arc",
                  IS_STRING(seg) ? "top (A)" : "lower-numbered point",
                  IS_STRING(seg) ? "bottom (ring)" : "higher-numbered point");
  } else {
    Serial.printf("\nShowing run %d\n", run + 1);
  }
  for (int r = 0; r < NUM_RUNS; r++) {
    if (runIsShown(r)) printShownRun(r);
  }
}

void stopShow() {
  showSeg = showRun = showPixel = -1;
  needsReset = true;
}

// LED index of slot p (0 = top, after skipping hidden LEDs) on string run r,
// or -1 if there is no LED there
int stringPixel(int r, int p) {
  p += runs[r].skip;
  if (p < 0 || p > runs[r].end - runs[r].start) return -1;
  return (runs[r].to == NODE_A) ? runs[r].end - p : runs[r].start + p;
}

bool isStringRun(int r) {
  return runs[r].from == NODE_A || runs[r].to == NODE_A;
}

void startPixel(int p) {
  showSeg = showRun = -1;
  showPixel = p;
  isFading = false;
  Serial.printf("\nPixel %d from the top on every string (+ / - to step, skip R K to shift):\n", p);
  for (int r = 0; r < NUM_RUNS; r++) {
    if (!isStringRun(r)) continue;
    int string = (runs[r].from == NODE_A ? runs[r].to : runs[r].from);
    int led = stringPixel(r, p);
    int n = runs[r].end - runs[r].start + 1;
    Serial.printf("  A-%d (run %2d, %2d LEDs, skip %2d): ", string, r + 1, n, runs[r].skip);
    if (led >= 0) Serial.printf("LED %d\n", led);
    else if (p + runs[r].skip < 0) Serial.println("gap at top, not lit");
    else Serial.println("past the bottom, not lit");
  }
}

void renderShow() {
  fill_solid(leds, NUM_LEDS, CRGB::Black);
  if (showPixel >= 0) {
    for (int r = 0; r < NUM_RUNS; r++) {
      int led = isStringRun(r) ? stringPixel(r, showPixel) : -1;
      if (led >= 0) leds[led] = CRGB::White;
    }
    return;
  }
  for (int r = 0; r < NUM_RUNS; r++) {
    if (!runIsShown(r)) continue;
    bool flip;
    segmentFor(runs[r].from, runs[r].to, flip);
    for (int i = runs[r].start; i <= runs[r].end; i++) leds[i] = CRGB::White;
    leds[flip ? runs[r].end : runs[r].start] = CRGB::Green;
    leds[flip ? runs[r].start : runs[r].end] = CRGB::Red;
  }
}

void printHelp() {
  Serial.println("Commands: tune = tune the LED map, map = print the map,");
  Serial.println("          reset = forget tuned map and use the defaults in the code");
  Serial.println("          show A-5 / show 1-2 = light a segment, run N = light run N (1-18),");
  Serial.println("          pixel N = light pixel N (0 = top) on every string, then + / - to step,");
  Serial.println("          skip R K = hide K LEDs at the top of string run R (negative: gap),");
  Serial.println("          off = back to patterns, speed N = set speed level 1-6");
}

void handleCommand(char *cmd) {
  while (*cmd == ' ') cmd++;
  int a, b;

  if (!tuning) {
    if (strcasecmp(cmd, "tune") == 0) startTuning();
    else if (strcasecmp(cmd, "map") == 0) printRuns();
    else if (strcasecmp(cmd, "reset") == 0) resetRuns();
    else if (strcasecmp(cmd, "off") == 0) stopShow();
    else if (sscanf(cmd, "speed %d", &a) == 1) {
      if (a < 1 || a > (int)NUM_SPEED_LEVELS) Serial.printf("  ?? Speed should be 1-%d\n", NUM_SPEED_LEVELS);
      else setSpeed(a - 1);
    }
    else if (strncasecmp(cmd, "show ", 5) == 0) {
      uint8_t seg = parseSegment(cmd + 5);
      if (seg == SEG_NONE) Serial.println("  ?? Segment should be like A-5 or 1-2");
      else startShow(seg, -1);
    } else if (sscanf(cmd, "skip %d %d", &a, &b) == 2) {
      int r = a - 1;
      if (r < 0 || r >= (int)NUM_RUNS || !isStringRun(r)) {
        Serial.println("  ?? skip R K: R must be a string run (see the pixel listing)");
      } else if (b >= runs[r].end - runs[r].start + 1 || b < -20) {
        Serial.println("  ?? Skip must be less than the run's LED count (and at least -20)");
      } else {
        runs[r].skip = b;
        buildMap();
        saveRuns();
        startPixel(showPixel >= 0 ? showPixel : 0);
      }
    } else if (sscanf(cmd, "pixel %d", &a) == 1) {
      if (a < 0) Serial.println("  ?? Pixel should be 0 or more");
      else startPixel(a);
    } else if (showPixel >= 0 && strcmp(cmd, "+") == 0) {
      startPixel(showPixel + 1);
    } else if (showPixel >= 0 && strcmp(cmd, "-") == 0) {
      startPixel(max(showPixel - 1, 0));
    } else if (sscanf(cmd, "run %d", &a) == 1) {
      if (a < 1 || a > (int)NUM_RUNS) Serial.printf("  ?? Run should be 1-%d\n", NUM_RUNS);
      else startShow(-1, a - 1);
    }
    else if (*cmd) printHelp();
    updateDisplay();
    return;
  }

  bool ok = true;
  if (*cmd == 0 || strcasecmp(cmd, "y") == 0) {
    if (++tuneRun >= NUM_RUNS) { finishTuning(); updateDisplay(); return; }
  } else if (strcasecmp(cmd, "b") == 0) {
    if (tuneRun > 0) tuneRun--;
  } else if (strcasecmp(cmd, "q") == 0) {
    finishTuning();
    updateDisplay();
    return;
  } else if (sscanf(cmd, "s %d", &a) == 1) {
    ok = setRunStart(tuneRun, a);
  } else if (sscanf(cmd, "e %d", &a) == 1) {
    ok = setRunEnd(tuneRun, a);
  } else if (sscanf(cmd, "%d %d", &a, &b) == 2) {
    // Set the end first when moving the run later, so start never passes end
    if (a > runs[tuneRun].end) ok = setRunEnd(tuneRun, b) && setRunStart(tuneRun, a);
    else ok = setRunStart(tuneRun, a) && setRunEnd(tuneRun, b);
  } else {
    ok = false;
  }

  if (!ok) Serial.println("  ?? Not valid here (out of range, or would overlap a neighbor)");
  buildMap();
  printTuneRun();
  updateDisplay();
}

// Reads a line at a time; accepts \n, \r or \r\n line endings
void handleSerial() {
  static char line[40];
  static int len = 0;
  static bool lastWasCR = false;

  while (Serial.available()) {
    char c = Serial.read();
    if (c == '\r' || c == '\n') {
      bool skip = (c == '\n' && lastWasCR);
      lastWasCR = (c == '\r');
      if (skip) continue;
      line[len] = 0;
      len = 0;
      handleCommand(line);
    } else {
      lastWasCR = false;
      if (len < (int)sizeof(line) - 1) line[len++] = c;
    }
  }
}

// ===== SETUP / LOOP =====
void setup() {
  auto cfg = M5.config();
  M5.begin(cfg);
  M5.Display.setRotation(1);

  // Release the pins held through deep sleep (see turnOff())
  bool wokeFromOff = (esp_sleep_get_wakeup_cause() == ESP_SLEEP_WAKEUP_EXT0);
  gpio_deep_sleep_hold_dis();
  gpio_hold_dis(POWER_HOLD_PIN);
  gpio_hold_dis((gpio_num_t)LED_PIN);

  Serial.begin(115200);

  FastLED.addLeds<CHIPSET, LED_PIN, COLOR_ORDER>(leds, NUM_LEDS).setCorrection(TypicalLEDStrip);
  FastLED.setBrightness(brightnessLevels[brightnessIndex]);
  fill_solid(leds, NUM_LEDS, CRGB::Black);
  FastLED.show();

  randomSeed(esp_random());
  loadRuns();

  shufflePlayOrder();
  loadSettings();
  if (wokeFromOff) autoMode = true;  // Turning back on with A always starts in auto mode
  FastLED.setBrightness(brightnessLevels[brightnessIndex]);
  patternStartTime = millis();
  M5.BtnA.setHoldThresh(AUTO_PRESS_MS);

  // After waking with A, wait for it to be released so that press isn't
  // taken as "next pattern"
  if (wokeFromOff) {
    // Waking leaves the A pin in RTC mode, where normal reads always see "pressed"
    rtc_gpio_deinit(BTN_A_PIN);
    pinMode(BTN_A_PIN, INPUT);
    waitForRelease();
    M5.update();
  }

  updateDisplay();
  Serial.println("Jasper Lights v" VERSION " ready! A: next pattern (hold 2 s: auto, 5 s: off), B: brightness, PWR: speed");
  Serial.printf("%s mode, starting with %s, brightness %d/%d, speed %d/%d\n",
                autoMode ? "Auto" : "Manual", patternNames[currentPattern],
                brightnessIndex + 1, NUM_BRIGHTNESS_LEVELS, speedIndex + 1, NUM_SPEED_LEVELS);
  printHelp();
}

void loop() {
  static unsigned long lastFrameTime = 0;
  unsigned long now = millis();
  if (now - lastFrameTime < FRAME_MS) return;
  lastFrameTime = now;

  handleSerial();

  M5.update();
  if (M5.BtnA.wasClicked()) {
    if (tuning) {
      char accept[] = "";
      handleCommand(accept);
    } else if (SHOWING) {
      stopShow();
    } else {
      if (autoMode) Serial.println("Manual mode");
      autoMode = false;
      nextPattern();
    }
    updateDisplay();
  }
  // Long presses: the screen says what releasing will do
  static uint8_t holdStage = 0;  // 1: held 2 s (auto), 2: held 5 s (off)
  if (M5.BtnA.isPressed()) {
    if (holdStage < 2 && M5.BtnA.pressedFor(OFF_PRESS_MS)) {
      holdStage = 2;
      showHoldScreen(true);
    } else if (holdStage < 1 && M5.BtnA.pressedFor(AUTO_PRESS_MS)) {
      holdStage = 1;
      showHoldScreen(false);
    }
  }
  if (M5.BtnA.wasReleased()) {
    if (holdStage == 2) turnOff();  // Doesn't return
    if (holdStage == 1) {
      if (!tuning && !SHOWING && !autoMode) {
        autoMode = true;
        patternStartTime = millis();
        saveSettings();
        Serial.println("Auto mode");
      }
      updateDisplay();
    }
    holdStage = 0;
  }
  if (holdStage == 2) {  // About to turn off: go dark now
    fill_solid(leds, NUM_LEDS, CRGB::Black);
    FastLED.show();
    return;
  }
  if (autoMode && !tuning && !SHOWING && millis() - patternStartTime >= AUTO_PATTERN_MS) {
    nextPattern();
    updateDisplay();
  }
  if (M5.BtnB.wasPressed()) {
    nextBrightness();
    updateDisplay();
  }
  // Power button (left side). A short press is safe; holding it ~6 s powers off.
  if (M5.BtnPWR.wasClicked()) {
    nextSpeed();
    updateDisplay();
  }

  if (tuning) renderTune();
  else if (SHOWING) renderShow();
  else renderPattern();
  FastLED.show();
}
