# Jasper Lights

LED controller for an M5StickC Plus2 driving a 200-LED WS2811 strand wrapped around a hanging ring structure (see [Structure map](#structure-map)). Derived from `m5lights_v1`, without the sound detection, ESP-NOW sync and Fluffy/E1.31 code.

## Use

Short version for everyday use: [USER_GUIDE.md](USER_GUIDE.md).

The lights start in **auto mode**: a new pattern every minute (`AUTO_PATTERN_MS`), always in the same order (numbered on the screen). Patterns cross-fade over 1.5 s.

- **A button** (big front M5 button) does everything important:
  - **Short press**: next pattern. It stays in auto or manual mode; in auto, the next change comes a full minute later. The screen shows AUTO or MAN.
  - **Hold 1 s** and release: turn **off** (LEDs dark, screen off, ESP32 in deep sleep). Once it's held long enough the screen says *Release to turn OFF*.
  - **Press while off**: turn back on.
- **Top button** (side button nearest the top, holding the M5 with the screen facing you and the writing upright): cycle through 6 brightness levels (4, 8, 15, 25, 40, 60), then back to the lowest. Starts at level 4 (25).
- **Bottom button** (the other side button; also the M5's power button): **hold 1 s** to switch between **auto and manual mode**. It switches (and the screen shows AUTO or MAN) as soon as 1 s is reached, so there's no reason to keep holding toward the ~6 s hardware power-off. Short press: cycle through 6 speed levels: 0.4x, 1x, 2x, 3.5x, 6x, 10x. Starts at level 2 (1x, the speed the patterns were designed at). Every pattern scales its motion by this, except the cross-fade and Map Check. Holding it ~6 s still turns the M5 off in hardware.

The screen shows the current pattern, and brightness and speed as rows of boxes.

It **turns itself off after 15 minutes** without button presses or serial commands (`AUTO_OFF_MS`).

Brightness, speed and the current pattern are saved in flash whenever they change and restored at power-up or when turned back on. It always starts in auto mode.

Off is a deep sleep. It keeps the M5's power-hold pin on, so the M5 wakes with the A button instead of needing the Bottom (power) button. The Plus2 can't switch off its 5V output in software, so the LED strip stays powered (dark) while off and its idle current is the main battery drain.

## Serial control

The serial monitor (115200 baud) takes the same controls as the buttons: `power on`, `power off`, `next`, `auto`, `manual`, `pattern N` or `pattern NAME`, `bright N` / `+` / `-`, `speed N` / `+` / `-` (levels 1-6). Anything but `power off` also turns the lights on. See `handleControl()`.

## WiFi remote control (built, but turned off)

`wifi_remote.ino` adds WiFi (a setup hotspot), remote control over MQTT (EMQX Cloud) and Google Home. It's **turned off** because WiFi made the LEDs glitch (green flashes, stutters), even with the LED driver's buffer at its maximum and the strip sent only once per frame. It's only compiled in when `secrets.h` exists; that file is now `secrets.h.off`. The Cloud Run / Google Home server side was not kept.

With it on: the Top button held 3 s opens a **Jasper Lights Setup** hotspot for choosing the home network; the screen's bottom right shows the connection status; "off" becomes a soft off that keeps listening (deep sleep below 3.65 V on battery, `LOW_BATTERY_MV`); and the serial commands above also arrive over MQTT on `jasperlights/cmd` (state goes out, retained, on `jasperlights/state`, `jasperlights/patterns` and `jasperlights/online`). Serial `wifi`, `wifi setup` and `wifi forget` manage it.

## Patterns

| # | Name           | Description                                        |
|---|----------------|----------------------------------------------------|
| 1 | Solid          | Whole strand fades slowly through the rainbow      |
| 2 | Cone Stripes   | Black & white: soft white bands drifting down (sometimes up) all strings; the ring glows as each arrives |
| 3 | Rainbow Spiral | Rainbow around the ring, twisting up the strings, slowly turning |
| 4 | Starry Night   | White stars slowly fading in and out on black      |
| 5 | Falling Rings  | A ring of light slowly slides down all six strings at once and lands on the wooden ring (sometimes rises instead) |
| 6 | Rainbow        | Rotating rainbow across the strand                 |
| 7 | Pinwheel       | Black & white: every other string white, the white slowly rotating around |
| 8 | Ripples        | Soft pastel waves drifting down the strings; the ring glows as each arrives |
| 9 | White Comet    | 1-3 white comets with soft tails gliding slowly on black |
| 10 | Rising Rainbow | Rainbow from A to the ring, drifting slowly up all strings together |
| 11 | B&W Stripes    | Wide black & white stripes drifting slowly         |
| 12 | Slow Orbit     | One or two soft pastel glows circling the structure, lighting each string as they pass |
| 13 | Breathe        | Whole strand gently breathing a soft pastel color  |
| 14 | B&W Spiral     | Black & white: stripes twisting down the strings like a barber pole, slowly turning |
| 15 | Sine Chase     | One color in waves with dark gaps, moving along    |
| 16 | Strings & Ring | Black & white: strings and ring slowly trade places, one white while the other is dark |
| 17 | Pastel Twinkle | Soft pastel lights slowly fading in and out on black |

The order is `patternList[]` in the code.

Cone Stripes, Rainbow Spiral, Falling Rings, Pinwheel, Ripples, Rising Rainbow, Slow Orbit, B&W Spiral and Strings & Ring use the structure map; the others treat the strand as one long line. The black & white ones are high contrast, which newborns see best.

Test patterns for checking the map (**Segment Map**: each segment in a fixed color; **Map Check**: red/green/blue then white dots moving down all strings) are left out of the rotation. Set `INCLUDE_TEST_PATTERNS` to 1 at the pattern list to include them.

Each time a pattern is selected it picks new random speed, direction and spacing.

### Smooth fades

The LEDs only have 256 steps per color, and at low brightness only a couple of dozen of those are used, so fades can stair-step. Patterns write 0-255 values on a perceptual scale into `leds[]`, and `showLeds()` turns them into light at 16-bit precision (smooth gamma curve, color correction, brightness level) before rounding to the LED's steps. Rounding uses spatial dithering: each LED has its own fixed threshold (evenly spread, in random order along the strand), so at a value of 3.4 steps about 40% of the LEDs show 4 and the rest show 3. In a fade, LEDs step up one at a time instead of all together, so large areas brighten smoothly, and since each LED changes only once per step nothing flickers. `HYSTERESIS` keeps an LED from chattering if its value hovers at its threshold. Only each LED's brightest channel is rounded this way; the other two are set in proportion, so all three step together and fading whites don't flash red or blue at their edges. Color correction fades in from none at the lowest step to full at `FULL_CORRECTION_LEVEL` (8), because at the bottom few steps it can't be represented and just tints whites lavender. (Time-based dithering was tried and dropped: it made dim pixels flicker.) Type `fps` in the serial monitor to see the strip refresh rate.

Moving patterns (White Comet, Falling Rings, Map Check) also track positions in 1/16ths of an LED and fade each LED in just ahead of the moving edge, so motion glides between LEDs instead of stepping.

## Structure map

The strand hangs on a cone: a small hanging ring **A** at the top, six strings **A-1 … A-6** down to six equally spaced points **1 … 6** on a wooden ring (~9.5" outside diameter, strings ~7"). LEDs are 1.5 cm apart. The ring arcs between points are **1-2, 2-3, 3-4, 4-5, 5-6, 6-1**.

The strand runs `A → 5 → 4 → 3 → A → 4 → 3 → 2 → 1 → A → 2 → 1 → 6 → 5 → A → 6 → 1 → 2 → 3`, with a jumper at A between LEDs 99 and 100. That covers A-5, 3-4, 2-3 and 6-1 twice and 1-2 three times; the other segments once. LEDs 197-199 are past the end of the structure (after point 3) and are not mapped, so structure patterns leave them dark. Each pass along one segment is a *run* in `defaultRuns[]`, with its first and last LED index, plus (for strings) a `skip` count of hidden LEDs at the top. `buildMap()` turns the runs into per-LED data that patterns can use:

| Array        | Meaning |
|--------------|---------|
| `ledSeg[]`   | Segment: 0-5 = strings A-1..A-6, 6-11 = arcs 1-2..6-1 (`SEG_NONE` if unmapped) |
| `ledPos[]`   | 0-255 along the segment: strings from A down, arcs from the lower point (6-1 from 6) |
| `ledDown[]`  | 0 at A, 255 at the wooden ring |
| `ledAngle[]` | 0-255 around the ring, point 1 = 0 |

Patterns should check `ledSeg[i] == SEG_NONE` and leave those LEDs black. Doubled segments get the same values on every copy, so they look like one segment.

Segment Map colors: strings A-1..A-6 are red, yellow, green, cyan, blue, magenta; arcs 1-2..6-1 are orange, lime, sea green, azure, violet, pink.

### Tuning the map

`defaultRuns[]` holds the map as tuned on the real structure (2026-09-26), including skips of -2 on A-3 and -3 on A-6. If the LEDs are moved or re-hung, re-tune: open the serial monitor (115200 baud, with a line ending) and type `tune`. For each run, in strand order:

- The run is lit white, its **first LED green** and **last LED red**. Everything else shows dimly in its Segment Map color.
- **Enter** or `y` (or the A button): accept and go to the next run
- `s N` / `e N`: set the start / end LED. `N M`: set both.
- `b`: back one run, `q`: finish early

Changing a run's end also moves the next run's start if they were touching (and the same for start). When you finish, the map is saved to flash and printed as C code; paste it over `defaultRuns[]` to make it permanent. A saved map overrides `defaultRuns[]` until you type `reset`.

Serial commands outside tuning: the remote commands above (`speed N`, `bright +`, …), `wifi`, `fps` (strip refresh rate), `tune`, `map` (print the current map as C code), `reset` (forget the saved map and use `defaultRuns[]`).

### Checking the map

- `show A-5` / `show 1-2`: lights every run covering that segment in white, with **green at the segment's start** and **red at its end** as the map understands them. Strings start at the top (A); ring arcs start at the lower-numbered point (6-1 starts at 6). A run with green at the wrong end is mapped backwards. The serial monitor lists each lit run and its LEDs.
- `run N`: lights just run N (1-18), the same way.
- `pixel N`: lights slot N from the top (0 = top) on every string, including both A-5 strips, to check they line up. `+` / `-` step down / up. The listing shows each string's run number, LED count and skip.
- `skip R K`: hides the top K LEDs of string run R (they stay dark), spacing the rest from A down to the ring; the bottom LED never moves. Negative K leaves a gap at the top instead. Saved to flash immediately. Use it when strings have extra LEDs bunched up at the hanging ring, so heights line up across strings. Each A-5 strip is its own run (1 and 14), so each gets its own skip. At 1.5 cm spacing a 7" string shows about 12 LEDs.
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

Write a `void myPattern(bool reset)` function that fills `leds[]` (and picks new parameters when `reset` is true). Advance its animation with `speedStep(step, carry)` rather than adding `step` directly, so it follows the speed setting; keep one `static int carry` per animated variable. Then add it, with its name, to `patternList[]`. Use `gammaRGB(r, g, b)` for colors (the gamma curve itself is applied in `showLeds()`).

Structure patterns use `ledSeg[]`, `ledPos[]`, `ledDown[]` and `ledAngle[]` (see above). For example, `fallingRings()` lights each LED by its `ledDown[]`, so a band moves down all six strings at once and reaches the ring together.

Note: the Arduino builder auto-generates function prototypes near the top of the file. A function that takes a struct defined in the sketch (like `twinkle(TwinkleState&, ...)`) needs its own prototype right after the struct, or it won't compile.

## Build

Board: **M5StickCPlus2** (`m5stack:esp32:m5stack_stickc_plus2`). Libraries: M5StickCPlus2, FastLED, and (only with WiFi on) WiFiManager (tzapu), PubSubClient and ArduinoJson.

```
jasper_lights/upload.sh             # build and upload (finds the M5's port)
jasper_lights/upload.sh --compile   # build only
arduino-cli monitor -p /dev/cu.usbserial-XXXX -c baudrate=115200
```

`upload.sh` passes compiler flags that give FastLED's LED driver its full buffer (8 RMT blocks), which matters if WiFi is ever turned back on. The Arduino IDE's Upload button can't pass them (the sketch prints a warning), but without WiFi the IDE works too.

Close the serial monitor before uploading; it holds the port.
