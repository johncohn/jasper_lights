/// @file    jasper_lights.ino
/// @brief   Simple M5StickC Plus2 LED blinker for Jasper
/// @version 1.3.0
/// @date    2026-09-26
/// @author  John Cohn (patterns adapted from m5lights_v1 / Larry's patterns)
///
/// A button (big front button): next pattern (patterns cross-fade)
/// B button (side button): cycle through 6 brightness levels
///
/// @changelog
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

#define VERSION "1.3.0"

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
};

const Run defaultRuns[] = {
  {NODE_A, 5,   0,  13},
  {5, 4,       14,  22},
  {4, 3,       23,  29},
  {3, NODE_A,  30,  43},
  {NODE_A, 4,  44,  57},
  {4, 3,       58,  65},
  {3, 2,       66,  74},
  {2, 1,       75,  84},
  {1, NODE_A,  85,  99},
  // Jumper at A
  {NODE_A, 2, 100, 115},
  {2, 1,      116, 125},
  {1, 6,      126, 133},
  {6, NODE_A, 134, 142},
  {NODE_A, 5, 143, 156},
  {5, 6,      157, 167},
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
    for (int i = 0; i < n; i++) {
      int led = runs[r].start + i;
      if (led < 0 || led >= NUM_LEDS) continue;
      uint8_t pos = (2 * i + 1) * 256 / (2 * n);  // Center of each LED's share
      if (flip) pos = 255 - pos;
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

// Used by Starry Night and Pastel Twinkle: lights fade in and out at random spots.
// Each pattern owns its own TwinkleState so they don't disturb each other when
// both are drawn during a cross-fade.
// phase 0 = off; 1..255 = rising then falling.
struct TwinkleState {
  uint8_t phase[NUM_LEDS];
  uint16_t hue[NUM_LEDS];
};

// Explicit prototype so the Arduino builder doesn't auto-generate one above the struct
void twinkle(TwinkleState &state, bool reset, bool pastel);

void twinkle(TwinkleState &state, bool reset, bool pastel) {
  uint8_t *phase = state.phase;
  uint16_t *hue = state.hue;

  if (reset) memset(phase, 0, sizeof(state.phase));

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
  static TwinkleState state;
  twinkle(state, reset, false);
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

// Pattern 8: Each string and ring arc in its own fixed color (see segmentColor()).
// Useful for checking the map: every copy of a doubled segment should match.
void segmentMap(bool reset) {
  for (int i = 0; i < NUM_LEDS; i++) {
    leds[i] = (ledSeg[i] == SEG_NONE) ? CRGB::Black : segmentColor(ledSeg[i], 255);
  }
}

// Pattern 9: A ring of light slides down all six strings at once, lands on the
// wooden ring and fades out. Sometimes it rises from the ring up to A instead.
void fallingRings(bool reset) {
  static int pos = 0;      // Leading edge, in 1/16ths of ledDown units
  static int speed = 8;
  static int hue = 0;
  static bool rising = false;
  const int tail = 90;     // Length of the glow behind the leading edge

  if (reset) {
    pos = 0;
    speed = 6 + random(6);  // ~7-11 s from A to the ring
    hue = random(1536);
    rising = random(3) == 0;
  }

  for (int i = 0; i < NUM_LEDS; i++) {
    if (ledSeg[i] == SEG_NONE) { leds[i] = CRGB::Black; continue; }
    int down = rising ? 255 - ledDown[i] : ledDown[i];
    int d = pos / 16 - down;  // How far the leading edge is past this LED
    byte v = 0;
    if (d >= 0 && d < tail) {
      int t = 255 * (tail - d) / tail;
      v = t * t / 255;
    }
    byte r, g, b;
    hsvToRgb(hue, 220, v, &r, &g, &b);
    leds[i] = gammaRGB(r, g, b);
  }

  pos += speed;
  if (pos / 16 > 255 + tail + 20) {  // Fully faded, plus a short dark pause
    pos = 0;
    hue = (hue + 256 + random(512)) % 1536;
  }
}

// Pattern 10: Rainbow from A down to the ring, drifting slowly up all strings together
void risingRainbow(bool reset) {
  static long offset = 0;   // In 1/16ths of a hue unit
  static int span = 768;    // Hue change from A to the ring

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

  offset = (offset + 8) % (1536L * 16);  // ~50 s per full rainbow cycle
}

// Pattern 11: Rainbow around the ring, twisting up the strings, slowly turning
void rainbowSpiral(bool reset) {
  static long offset = 0;   // In 1/16ths of a hue unit
  static int twist = 512;   // Extra hue change from A to the ring

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

  offset = (offset + 6) % (1536L * 16);  // ~70 s per turn
}

// Pattern 12: One or two soft pastel glows slowly circling the structure. Each
// string lights up as a glow passes it, with the ring glowing underneath.
void slowOrbit(bool reset) {
  static long angle = 0;   // In 1/16ths of a ledAngle unit
  static int speed = 3;
  static int glows = 1;
  static int hue = 0;

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

  angle = (angle + speed + 256L * 16) % (256L * 16);
  hue = (hue + 1) % 1536;
}

// Pattern 13: Soft pastel waves drifting down the strings; the ring glows as each
// wave arrives
void ripples(bool reset) {
  static int phase = 0;
  static int hue = 0;

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

  phase = (phase + 719) % 720;  // Step back one unit: waves move down, ~12 s each
  hue = (hue + 1) % 1536;
}

// ===== PATTERN LIST =====
// To add a pattern: write a function like the ones above, then add it here
// and give it a name in patternNames[].
typedef void (*Pattern)(bool reset);
Pattern gPatterns[] = {
  solidColor, rainbow, sineWaveChase,
  bwStripes, starryNight, whiteComet, breathe, pastelTwinkle,
  segmentMap, fallingRings, risingRainbow, rainbowSpiral, slowOrbit, ripples
};
const char* patternNames[] = {
  "Solid", "Rainbow", "Sine Chase",
  "B&W Stripes", "Starry Night", "White Comet", "Breathe", "Pastel Twinkle",
  "Segment Map", "Falling Rings", "Rising Rainbow", "Rainbow Spiral", "Slow Orbit",
  "Ripples"
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

// ===== MAP STORAGE =====
// Only the start/end of each run are saved, so a change to the strand path in
// defaultRuns[] (different number of runs) makes old saved values be ignored.
Preferences prefs;

void loadRuns() {
  memcpy(runs, defaultRuns, sizeof(runs));
  int16_t saved[NUM_RUNS * 2];
  prefs.begin("jlmap", true);
  if (prefs.getBytesLength("runs") == sizeof(saved)) {
    prefs.getBytes("runs", saved, sizeof(saved));
    for (int r = 0; r < NUM_RUNS; r++) {
      runs[r].start = saved[r * 2];
      runs[r].end = saved[r * 2 + 1];
    }
    Serial.println("Loaded tuned map from flash");
  }
  prefs.end();
  buildMap();
}

void saveRuns() {
  int16_t saved[NUM_RUNS * 2];
  for (int r = 0; r < NUM_RUNS; r++) {
    saved[r * 2] = runs[r].start;
    saved[r * 2 + 1] = runs[r].end;
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
    Serial.printf("  {%s, %s, %3d, %3d},  // %s -> %s, %d LEDs\n",
                  runs[r].from == NODE_A ? "NODE_A" : nodeNames[runs[r].from],
                  runs[r].to == NODE_A ? "NODE_A" : nodeNames[runs[r].to],
                  runs[r].start, runs[r].end,
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

void printHelp() {
  Serial.println("Commands: tune = tune the LED map, map = print the map,");
  Serial.println("          reset = forget tuned map and use the defaults in the code");
}

void handleCommand(char *cmd) {
  while (*cmd == ' ') cmd++;
  int a, b;

  if (!tuning) {
    if (strcasecmp(cmd, "tune") == 0) startTuning();
    else if (strcasecmp(cmd, "map") == 0) printRuns();
    else if (strcasecmp(cmd, "reset") == 0) resetRuns();
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

  Serial.begin(115200);

  FastLED.addLeds<CHIPSET, LED_PIN, COLOR_ORDER>(leds, NUM_LEDS).setCorrection(TypicalLEDStrip);
  FastLED.setBrightness(brightnessLevels[brightnessIndex]);
  fill_solid(leds, NUM_LEDS, CRGB::Black);
  FastLED.show();

  randomSeed(esp_random());
  loadRuns();

  updateDisplay();
  Serial.println("Jasper Lights v" VERSION " ready! A: next pattern, B: brightness");
  printHelp();
}

void loop() {
  static unsigned long lastFrameTime = 0;
  unsigned long now = millis();
  if (now - lastFrameTime < FRAME_MS) return;
  lastFrameTime = now;

  handleSerial();

  M5.update();
  if (M5.BtnA.wasPressed()) {
    if (tuning) {
      char accept[] = "";
      handleCommand(accept);
    } else {
      nextPattern();
    }
    updateDisplay();
  }
  if (M5.BtnB.wasPressed()) {
    nextBrightness();
    updateDisplay();
  }

  if (tuning) renderTune();
  else renderPattern();
  FastLED.show();
}
