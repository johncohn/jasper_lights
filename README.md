# Jasper Lights

LED controller for an M5StickC Plus2 driving a 200-LED WS2811 strand wrapped around a hanging ring structure (see [Structure map](#structure-map)). Derived from `m5lights_v1`, without the sound detection, ESP-NOW sync and Fluffy/E1.31 code.

## Use

- **A button** (big front button): go to the next pattern. Patterns cross-fade over 1.5 s.
- **B button** (side button): cycle through 6 brightness levels (4, 8, 15, 25, 40, 60), then back to the lowest. Starts at level 4 (25). The screen shows the current level.

## Patterns

| # | Name       | Description                                        |
|---|------------|----------------------------------------------------|
| 0 | Solid      | Whole strand fades slowly through the rainbow      |
| 1 | Rainbow    | Rotating rainbow across the strand                 |
| 2 | Sine Chase | One color in waves with dark gaps, moving along     |
| 3 | B&W Stripes    | Wide black & white stripes drifting slowly     |
| 4 | Starry Night   | White stars slowly fading in and out on black  |
| 5 | White Comet    | 1-3 white comets with soft tails gliding on black |
| 6 | Breathe        | Whole strand gently breathing a soft pastel color |
| 7 | Pastel Twinkle | Soft pastel lights slowly fading in and out on black |
| 8 | Segment Map    | Each string and ring arc in its own fixed color (see below) |
| 9 | Falling Rings  | A ring of light slowly slides down all six strings at once and lands on the wooden ring (sometimes rises instead) |
| 10 | Rising Rainbow | Rainbow from A to the ring, drifting slowly up all strings together |
| 11 | Rainbow Spiral | Rainbow around the ring, twisting up the strings, slowly turning |
| 12 | Slow Orbit     | One or two soft pastel glows circling the structure, lighting each string as they pass |
| 13 | Ripples        | Soft pastel waves drifting down the strings; the ring glows as each arrives |
| 14 | Map Check      | Diagnostic, repeats every 20 s: whole structure red, green, blue (2 s each), then white dots moving from A down all strings together |

Patterns 8-13 use the structure map and are all slow and gentle; the others treat the strand as one long line.

Patterns 3-7 are meant for a baby: slow, and mostly high-contrast black & white, which newborns see best.

Each time a pattern is selected it picks new random speed, direction and spacing.

Moving patterns (White Comet, Falling Rings, Map Check) track positions in 1/16ths of an LED and fade each LED in just ahead of the moving edge, so motion glides between LEDs instead of stepping. At low brightness levels each LED has only a few distinct brightness steps, so the dimmest parts of fades can still look a little steppy.

## Structure map

The strand hangs on a cone: a small hanging ring **A** at the top, six strings **A-1 … A-6** down to six equally spaced points **1 … 6** on a wooden ring (~9.5" outside diameter, strings ~7"). LEDs are 1.5 cm apart. The ring arcs between points are **1-2, 2-3, 3-4, 4-5, 5-6, 6-1**.

The strand runs `A → 5 → 4 → 3 → A → 4 → 3 → 2 → 1 → A → 2 → 1 → 6 → 5 → A → 6 → 1 → 2 → 3`, with a jumper at A between LEDs 99 and 100. That covers A-5, 3-4, 2-3 and 6-1 twice and 1-2 three times; the other segments once. LEDs 197-199 are past the end of the structure (after point 3) and are not mapped, so structure patterns leave them dark. Each pass along one segment is a *run* in `defaultRuns[]`, with its first and last LED index. `buildMap()` turns the runs into per-LED data that patterns can use:

| Array        | Meaning |
|--------------|---------|
| `ledSeg[]`   | Segment: 0-5 = strings A-1..A-6, 6-11 = arcs 1-2..6-1 (`SEG_NONE` if unmapped) |
| `ledPos[]`   | 0-255 along the segment: strings from A down, arcs from the lower point (6-1 from 6) |
| `ledDown[]`  | 0 at A, 255 at the wooden ring |
| `ledAngle[]` | 0-255 around the ring, point 1 = 0 |

Patterns should check `ledSeg[i] == SEG_NONE` and leave those LEDs black. Doubled segments get the same values on every copy, so they look like one segment.

Segment Map colors: strings A-1..A-6 are red, yellow, green, cyan, blue, magenta; arcs 1-2..6-1 are orange, lime, sea green, azure, violet, pink.

### Tuning the map

`defaultRuns[]` holds the map as tuned on the real structure (2026-09-26). If the LEDs are moved or re-hung, re-tune: open the serial monitor (115200 baud, with a line ending) and type `tune`. For each run, in strand order:

- The run is lit white, its **first LED green** and **last LED red**. Everything else shows dimly in its Segment Map color.
- **Enter** or `y` (or the A button): accept and go to the next run
- `s N` / `e N`: set the start / end LED. `N M`: set both.
- `b`: back one run, `q`: finish early

Changing a run's end also moves the next run's start if they were touching (and the same for start). When you finish, the map is saved to flash and printed as C code; paste it over `defaultRuns[]` to make it permanent. A saved map overrides `defaultRuns[]` until you type `reset`.

Serial commands outside tuning: `tune`, `map` (print the current map as C code), `reset` (forget the saved map and use `defaultRuns[]`).

### Checking the map

- `show A-5` / `show 1-2`: lights every run covering that segment in white, with **green at the segment's start** and **red at its end** as the map understands them. Strings start at the top (A); ring arcs start at the lower-numbered point (6-1 starts at 6). A run with green at the wrong end is mapped backwards. The serial monitor lists each lit run and its LEDs.
- `run N`: lights just run N (1-18), the same way.
- `pixel N`: lights the Nth LED from the top (0 = top) on every string, including both A-5 strips, to check they line up. `+` / `-` step down / up. Strings with fewer than N+1 LEDs are listed as not lit.
- `off` or the A button: back to the patterns.

If you've lost track of which point is which, `show A-1`, `show A-2`, … find the points and which way the numbers go around the ring.

## Hardware config (top of `jasper_lights.ino`)

| Setting      | Default | Notes                                   |
|--------------|---------|-----------------------------------------|
| `LED_PIN`    | 32      | Grove port data pin                     |
| `NUM_LEDS`   | 200     |                                         |
| `brightnessLevels[]` | 4, 8, 15, 25, 40, 60 | 25 was the m5lights_v1 value, kept low for M5Stick 5V power stability. The top levels draw much more current. |
| `CHIPSET`    | WS2811  |                                         |
| `COLOR_ORDER`| GRB     |                                         |

## Adding a pattern

Write a `void myPattern(bool reset)` function that fills `leds[]` (and picks new parameters when `reset` is true). Then add it to `gPatterns[]` and give it a name in `patternNames[]`.

Structure patterns use `ledSeg[]`, `ledPos[]`, `ledDown[]` and `ledAngle[]` (see above). For example, `fallingRings()` lights each LED by its `ledDown[]`, so a band moves down all six strings at once and reaches the ring together.

Note: the Arduino builder auto-generates function prototypes near the top of the file. A function that takes a struct defined in the sketch (like `twinkle(TwinkleState&, ...)`) needs its own prototype right after the struct, or it won't compile.

## Build

Board: **M5StickCPlus2** (`m5stack:esp32:m5stack_stickc_plus2`). Libraries: M5StickCPlus2, FastLED.

```
arduino-cli compile --fqbn m5stack:esp32:m5stack_stickc_plus2 jasper_lights
arduino-cli compile --fqbn m5stack:esp32:m5stack_stickc_plus2 -u -p /dev/cu.usbserial-XXXX jasper_lights
arduino-cli monitor -p /dev/cu.usbserial-XXXX -c baudrate=115200
```

Close the serial monitor before uploading; it holds the port.
