# Linux Video Options

This note describes the current display-related options in the Linux Qt client.

## Display Aspect

Available from `View > Display Aspect`.

### Auto

- Uses the emulator-reported display geometry and pixel aspect ratio.
- This is the most faithful to the raw framebuffer shape coming from Beebium.
- In practice, it can look a little horizontally stretched or compressed depending on mode and window size.

### 4:3

- Forces the displayed picture area to a classic 4:3 presentation.
- This is often the most natural-looking option for BBC Micro output on a modern display.
- If you want a CRT-like overall shape, this is usually the best default.

### Square Pixels

- Ignores the BBC pixel-aspect correction and uses the raw framebuffer aspect.
- This can be useful for debugging or if you want the least interpreted presentation.

## Display Presentation

Available from `View > Display Presentation`.

### Texture Sampling

#### Nearest

- Uses nearest-neighbour filtering when scaling the final framebuffer texture.
- Best for sharp pixel edges.
- Usually the preferred option for Mode 7 experiments.

#### Linear

- Uses linear filtering when scaling the final framebuffer texture.
- Softer look.
- Can reduce harsh stepping, but may blur teletext and text edges.

### Integer Scale

- Forces the displayed framebuffer to the largest whole-number multiple of the source frame that fits in the available area.
- This gives the cleanest pixel structure.
- Tradeoff: the image can become smaller than the available space if the next whole-number step would not fit.

When to use it:

- when sharpness matters more than filling the window
- when you want the most stable-looking teletext or pixel graphics

### Constrain To Integer Multiples

- Applies a softer version of integer scaling.
- The display is still fitted to the available area first, but if an integer multiple fits cleanly, the final size is snapped down to that integer multiple.
- This keeps the image closer to the normal fitted size while still avoiding some uneven scaling.

When to use it:

- when you want cleaner scaling than free-fit
- but do not want the stricter behavior of full `Integer Scale`

## Why Integer Scale and Constrain To Integer Multiples can look the same

They can appear identical when:

- the current window size only allows one sensible integer multiple anyway, or
- the best fitted size already lands very close to that same integer multiple

In those cases:

- `Integer Scale` picks that integer multiple directly
- `Constrain To Integer Multiples` also ends up snapping to the same size

So it is normal for them to sometimes look identical.

## Why Aspect options can also look similar once integer modes are enabled

When either integer mode is enabled, the chosen aspect often gets constrained by the integer step that actually fits in the window.

That means:

- `Auto`
- `4:3`
- `Square Pixels`

may converge visually if the same integer-scaled rectangle is the best fit for all of them at the current window size.

This is especially noticeable when the display area is not very large.

## Suggested starting points

### Most natural general presentation

- `Display Aspect`: `4:3`
- `Texture Sampling`: `Nearest`
- `Constrain To Integer Multiples`: on

### Sharpest / most pixel-stable presentation

- `Display Aspect`: `4:3`
- `Texture Sampling`: `Nearest`
- `Integer Scale`: on

### Softened presentation

- `Display Aspect`: `4:3`
- `Texture Sampling`: `Linear`
- integer options: off

## Current limitation

These options are all client-side presentation controls.

They do not change how Mode 7 glyphs are generated inside the emulator core. They only change how the already-rendered framebuffer is shown in the Linux client.
