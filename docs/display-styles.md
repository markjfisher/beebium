# Display Styles

This document describes the macOS client's Display Style system: the
abstraction that controls how an emulator frame is presented on screen,
including the Metal pipeline used, the geometry rules, and the SwiftUI
options panel exposed under the Video sidebar.

## Overview

The macOS client's renderer is split into two layers:

1. **`MetalRenderer`** is a thin coordinator. It holds the frame texture,
   caches Metal pipeline states, owns the per-frame draw loop, and forwards
   per-frame data to the active style.
2. **`DisplayStyle`** implementations own the *presentation* decisions:
   which Metal vertex/fragment functions to compile, how to fill the
   per-frame uniform buffer, and which SwiftUI options view to show under
   the Video sidebar.

This split means new ways to display a BBC frame can be added without
piling conditionals into the renderer. Each style is self-contained.

The macOS client ships with two styles today:

- **Standard** -- the user-facing default. Fits the active pixel area into
  the drawable with an inner edge-margin frame. CRTC blanking outside the
  picture rectangle shows as letterbox or pillarbox in the configurable
  window background colour.
- **Debug** -- preserves the developer-oriented view that shipped before
  the system was introduced. Aspect-fits the active+border rectangle and
  paints four distinct coloured borders showing the CRTC's overscan
  tracking.

A future **CRT** style is planned (scanlines, phosphor mask, optional
curvature). The architecture is designed for it; the file map and the
"adding a style" recipe below describe how it slots in.

## Architectural model

Display Style is one of three independent dimensions that determine what
the user sees:

```
                 Display Style                Pixel Shape         Content Source
                 -------------                -----------         --------------
                 Standard                     Authentic           Bitmap (today)
                 Debug                        Crisp               Vector MODE 7 (planned)
                 (CRT, planned)
```

- **Display Style** answers *how is the frame presented?* (borders,
  geometry, post-processing).
- **Pixel Shape** answers *what shape is one BBC pixel on the host
  display?* PAR 0.96 (Authentic) or PAR 1.0 (Crisp).
- **Content Source** answers *where do the pixels come from?* Today
  always bitmap from the server. The proposed vector MODE 7 path
  (`docs/discussion/vector-mode-7.md`) is a future content-source variant
  that composes with any Display Style.

Modelling these as orthogonal avoids a Cartesian product of permutations
inside any single abstraction. Each dimension is a separate setting on
`VideoSettings`.

## File map

```
clients/macos/Beebium/Beebium/Display/
    DisplayStyle.swift          Protocol + value types + Uniforms struct
    DebugDisplayStyle.swift     Today's coloured-border behaviour
    StandardDisplayStyle.swift  Default user-facing style
    PixelShape.swift            Authentic / Crisp enum
    VideoSettings.swift         Per-window settings ObservableObject
    VideoSettingsCache.swift    In-memory per-machine cache
    ColorExtensions.swift       Hex parsing/formatting, Metal/SIMD packing
    ResettableControls.swift    Reset-to-default UI components

clients/macos/Beebium/Beebium/
    Shaders.metal               vertexShader + fragmentShader (Debug)
                                + fragmentShaderStandard
    MetalRenderer.swift         Renderer coordinator
    EmulatorView.swift          NSViewRepresentable wiring
    SidebarModeContent.swift    Video sidebar view (VideoModeView)

clients/macos/Beebium/Beebium/Settings/
    VideoSettingsPane.swift     Settings > Video pane (global defaults)

clients/macos/Beebium/BeebiumTests/
    DebugDisplayStyleTests.swift
    StandardDisplayStyleTests.swift
    VideoSettingsTests.swift           (style picker + edge margin)
    VideoSettingsLoadTests.swift       (UserDefaults load paths)
    VideoSettingsCacheTests.swift      (per-machine cache lifecycle)
    PixelShapeTests.swift
    ColorExtensionsTests.swift
```

## The DisplayStyle protocol

```swift
protocol DisplayStyle: AnyObject {
    var id: String { get }
    var displayName: String { get }

    func makePipelineState(device: MTLDevice,
                           pixelFormat: MTLPixelFormat) throws -> MTLRenderPipelineState

    func makeUniforms(frame: FrameContext, drawable: DrawableContext) -> Uniforms

    @MainActor func makeOptionsView() -> AnyView
}
```

- `id` is stable and used for both pipeline caching and persistence keys
  (`@AppStorage`, snapshot fields). Treat changes as a breaking change for
  saved preferences.
- `displayName` is the user-visible label shown in the sidebar picker.
- `makePipelineState` is called once per `(style, MTLDevice)` pair; the
  renderer caches the result and reuses it across style switches.
- `makeUniforms` MUST be a pure function of its inputs. No Metal calls,
  no side effects. Tests rely on this -- they verify the produced values
  without an `MTLDevice`.
- `makeOptionsView` returns the SwiftUI panel shown under the active
  style's section in the Video sidebar.

`DisplayStyle` is reference-typed (`AnyObject`) so styles with tweakable
parameters can conform to `ObservableObject` and use `@Published`
properties (e.g. `StandardDisplayStyle.edgeMarginColor`). Style instances
are owned per-window via `VideoSettings.availableStyles`; per-style
parameters do not leak between windows.

## Per-frame value types

Two value types capture everything `makeUniforms` needs:

```swift
struct FrameContext: Equatable {
    let textureWidth, textureHeight: Int
    let displayWidth, displayHeight: Int
    let leftBorder, rightBorder, topBorder, bottomBorder: Int
    let interlaced: Bool
    let regions: [DisplayRegion]
}

struct DrawableContext: Equatable {
    let drawableSize: CGSize
    let parScale: Float                  // 0.96 Authentic, 1.0 Crisp
}
```

`FrameContext` is the data the gRPC frame stream delivers (texture,
display, borders, interlace state, split-screen regions).
`DrawableContext` is what the host window contributes (drawable size in
pixels, current Pixel Shape).

Pure value types so style implementations are unit-testable without a
Metal device.

## Uniforms struct contract

Both Swift and Metal define a `Uniforms` struct. They MUST be
byte-identical:

| Offset | Field                | Type            |
|-------:|----------------------|-----------------|
|      0 | drawableSize         | float2          |
|      8 | textureSize          | float2          |
|     16 | displaySize          | float2          |
|     24 | totalSize            | float2          |
|     32 | borderOffset         | float2          |
|     40 | parScale             | float           |
|     44 | interlaced           | uint            |
|     48 | leftBorderColor      | float4 (Debug)  |
|     64 | rightBorderColor     | float4 (Debug)  |
|     80 | topBorderColor       | float4 (Debug)  |
|     96 | bottomBorderColor    | float4 (Debug)  |
|    112 | edgeMarginColor      | float4 (Std)    |
|    128 | regionCount          | uint            |
|    132 | edgeMargin           | float           |
|    136 | _pad1, _pad2         | uint, uint      |
|    144 | regions[8]           | RegionUniforms  |

Total: 272 bytes.

`float4` requires 16-byte alignment, so the colour fields all start at
offsets divisible by 16. The Swift struct uses explicit `_pad1`/`_pad2`
slots to preserve the alignment of the `regions` array.

When extending the struct (e.g. for a CRT style):

1. Add fields to both the Swift `Uniforms` struct and the Metal
   `Uniforms` struct in the same order, with matching types.
2. Place `float4` and similar 16-byte-aligned types at offsets divisible
   by 16. Use existing padding slots first; failing that, append at the
   end before `regions`.
3. Existing styles that do not use the new field should set it to a
   neutral default (zero / identity) to protect against fragment-shader
   changes that might later read it.

## VideoSettings: per-window settings

`VideoSettings` is the per-window `ObservableObject` owned by
`ContentView` via `@StateObject`. It holds:

- `activeStyleID: String` -- which style is selected.
- `availableStyles: [any DisplayStyle]` -- the styles the picker offers.
- `pixelShape: PixelShape` -- Authentic or Crisp.
- `windowBackground: Color` -- letterbox/pillarbox fill colour.

Per-style state (e.g. Standard's `edgeMargin` and `edgeMarginColor`)
lives on the style instance, not on `VideoSettings`. Each window has its
own style instances, so per-style state is per-window automatically.

`VideoSettings.loadFromUserDefaults(_:)` is the production constructor
used by `ContentView`. It reads the global defaults from
`UserDefaults` (see "Persistence" below). The explicit-arg `init` is the
test path -- it bypasses `UserDefaults` entirely so test runs do not
mutate or depend on host state.

## Persistence

Three layers, in order of precedence at window creation:

1. **Per-machine in-memory cache** (`VideoSettingsCache.shared`,
   keyed by `SystemClient.machineUUID`). Restored when a window connects
   to a machine seen in this client session, saved on every change.
   Process-scoped: dies with the macOS client.
2. **Global defaults** (`@AppStorage` via Settings > Video). Used when
   no per-machine snapshot exists. Persisted across client launches.
3. **Built-in defaults** (constants on `VideoSettings`,
   `StandardDisplayStyle`, `PixelShape`). Used when neither layer 1
   nor 2 produces a value.

The cache snapshot intentionally captures only the
`VideoSettings`-owned fields (`activeStyleID`, `pixelShape`,
`windowBackground`). Per-style parameters are not persisted; they reset
to their per-window defaults when a snapshot is restored. This is a
deliberate scope cut and the snapshot schema can grow when there is
more cross-machine state worth preserving.

Persistence across server restarts and client launches (i.e. saving the
per-machine cache to disk keyed on a more stable identifier than
`MachineIdentity.uuid`) is a planned refinement.

### UserDefaults keys

```
video.defaultStyleID            String   default "standard"
video.defaultPixelShape         String   default "authentic" (PixelShape.rawValue)
video.defaultWindowBackgroundHex String  default "#262626"
```

Stable across releases; reading is defensive against unknown values
(`PixelShape(rawValue:)` returning `nil` falls back to Authentic, etc.).

## Renderer dispatch

Per frame, in `MetalRenderer.draw(in:)`:

1. Look up the cached pipeline for the active style.
2. Build a `FrameContext` from the most recent gRPC frame data and a
   `DrawableContext` from the MTKView's drawable size and the current
   Pixel Shape.
3. Call `activeStyle.makeUniforms(frame:drawable:)`.
4. Set the uniform buffer, set the texture, draw the unit quad.

Style switches happen via `MetalRenderer.setActiveStyle(_:)` from
`EmulatorView.updateNSView`. Pipelines are lazily built on first
activation and cached forever. Changes to per-window settings
(`videoSettings.pixelShape`, `videoSettings.windowBackground`,
`videoSettings.activeStyle.edgeMargin` for Standard, ...) take effect on
the next frame because the renderer reads them through the live
`VideoSettings` reference each draw.

## Standard's geometry

The Standard style introduces an inner edge-margin frame. The vertex
shader fits the picture rectangle to the drawable at full size (no
shrinkage). The Standard fragment shader then classifies each fragment:

```metal
if (in.texCoord.x < margin || in.texCoord.x > (1.0 - margin) ||
    in.texCoord.y < margin || in.texCoord.y > (1.0 - margin)) {
    return uniforms.edgeMarginColor;
}
// Inside the active rectangle
float scale = 1.0 - 2.0 * margin;
float2 activeCoord = (in.texCoord - float2(margin, margin)) / scale;
// ... sample texture using activeCoord ...
```

So the active pixels occupy `(1 - 2 * edgeMargin)` of the picture
rectangle on each axis. The frame around them is `edgeMarginColor`.
Beyond the picture rectangle, the MTKView clear colour shows -- this is
the `windowBackground`.

Setting `edgeMarginColor == windowBackground` collapses the visible
boundary between the two, but the active pixels remain at the same
on-screen size: the inner-frame geometry is independent of colour.

Debug uses the same vertex shader; its fragment shader uses
`borderOffset` and `displaySize` to identify the four CRTC border
regions and paints them in their respective colours. Debug always sets
`edgeMargin` to zero, so the inner-frame logic in the vertex/fragment
chain is a no-op for it.

## Reset-to-default UI

User-tweakable values (window background, edge margin colour, edge
margin size, default window background) all expose a small reset
chevron beside the control plus a "Reset to Default" item in the
control's right-click context menu. Both are disabled when the value is
already at default.

The chevron stays in the layout (just dimmed) when the value is at
default, so toggling default state does not shift neighbouring
controls.

Two reusable components implement the pattern:

- `ResettableColorPicker` -- ColorPicker with reset chevron + context
  menu. Equality is via sRGB hex.
- `ResettablePercentStepper` -- Stepper bound to an integer percentage
  with the same reset affordances.

Both are in `Display/ResettableControls.swift`. Adding the same
treatment to a future control is small.

## Adding a new Display Style

For a hypothetical CRT style:

1. Add the Metal entry points (e.g. `vertexShaderCrt`,
   `fragmentShaderCrt`) to `Shaders.metal`. Reuse the shared
   `vertexShader` if the geometry rules match.
2. Extend the `Uniforms` struct (Swift + Metal) with any new fields
   the CRT shader needs (e.g. scanline intensity). Place new
   16-byte-aligned types at 16-aligned offsets; reuse existing padding
   slots before appending.
3. Create `Display/CrtDisplayStyle.swift`. Declare it as a
   `final class CrtDisplayStyle: DisplayStyle, ObservableObject` if it
   has tweakable parameters; otherwise `final class CrtDisplayStyle:
   DisplayStyle` is sufficient.
4. Implement `makePipelineState` referencing the new shader entry
   points, `makeUniforms` populating only the fields the CRT shader
   reads (and zeroing fields it does not, for hygiene), and
   `makeOptionsView` returning a SwiftUI panel bound to the
   `@Published` parameters.
5. Add the new style to the default `availableStyles` array in
   `VideoSettings.init`, in the order it should appear in the picker.
6. Add unit tests under `BeebiumTests/`. Mirror the pattern in
   `StandardDisplayStyleTests.swift`: identity fields, geometry
   propagation, mode-flag propagation, region packing, defensive
   handling of clamping and unknown values.
7. Run `xcodegen generate` and rebuild.

The `MetalRenderer` does not need to change. The `VideoModeView` picker
picks up new styles automatically from `VideoSettings.availableStyles`.

## Composition with Vector MODE 7

The proposed vector MODE 7 path
(`docs/discussion/vector-mode-7.md`) is a content-sourcing alternative
for MODE 7 frames specifically. It composes with any Display Style
because the rasterised text lands in the same `MTLTexture` that the
active Display Style's pipeline already consumes. The choice is exposed
through a single `mode7VectorEnabled` toggle on `VideoSettings`,
orthogonal to the active style.

When CRT lands, it may want to opt out of vector content replacement
because the analog raster look depends on the SAA5050 output. The
expected protocol addition is:

```swift
protocol DisplayStyle: AnyObject {
    // ...
    var supportsVectorContentReplacement: Bool { get }
}

extension DisplayStyle {
    var supportsVectorContentReplacement: Bool { true }
}
```

Standard and Debug accept the default `true`. CRT overrides to
`false`. The renderer dispatches accordingly.

## Testability and limitations

Unit tests cover:

- Protocol contracts (`id`, `displayName` stability).
- `makeUniforms` value semantics: geometry, mode flags, colour
  propagation, edge-margin propagation, region packing including
  `> MAX_REGIONS` clamping.
- `VideoSettings` selection behaviour, defaults, fallbacks for unknown
  ids, `objectWillChange` emission.
- `loadFromUserDefaults` paths under a private `UserDefaults` suite
  per test, including invalid persisted values.
- `VideoSettingsCache` lifecycle: empty, save/get, replace, UUID
  isolation, empty-UUID rejection, snapshot capture and apply round
  trips, defensive handling of unknown style ids and malformed colour
  hex.
- `Color` extensions: hex parsing edge cases, hex formatting, Metal
  clear-colour conversion, SIMD4 packing.

What is NOT covered by automated tests:

- Pixel-level rendering correctness (Metal output is not
  introspected).
- SwiftUI layout of the sidebar panels and the Settings >Video
  pane.
- Whether style switches visibly change the picture.

These need eyeball verification on the running app before merging
substantive changes.
