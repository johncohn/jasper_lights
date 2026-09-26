# Jasper Lights

Simple LED blinker for an M5StickC Plus2 driving a WS2811 strand. Derived from `m5lights_v1`, without the sound detection, ESP-NOW sync and Fluffy/E1.31 code.

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
| 4 | Starry Night   | White stars fading in and out on black         |
| 5 | White Comet    | 1-3 white comets with soft tails gliding on black |
| 6 | Breathe        | Whole strand gently breathing a soft pastel color |
| 7 | Pastel Twinkle | Soft pastel lights fading in and out on black  |

Patterns 3-7 are meant for a baby: slow, and mostly high-contrast black & white, which newborns see best.

Each time a pattern is selected it picks new random speed, direction and spacing.

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

## Build

Board: **M5StickCPlus2** (`m5stack:esp32:m5stack_stickc_plus2`). Libraries: M5StickCPlus2, FastLED.

```
arduino-cli compile --fqbn m5stack:esp32:m5stack_stickc_plus2 jasper_lights
```
