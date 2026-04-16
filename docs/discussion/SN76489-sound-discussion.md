# SN76489 Sound Chip Overview

## Prompt:
I was to discuss SN76489 sound chip emulation.  I gather this device has three independent square wave channels, and one noise channel. Give me an overview of its workings and capabilities.

## Response:
The **SN76489** (and close variants like the SN76496) is a very simple but historically important **programmable sound generator (PSG)**. It’s the chip behind the sound of many late-1970s and early-1980s systems (BBC Micro, TI-99/4A, ColecoVision, Sega Master System, etc.). Its design is minimalistic, deterministic, and very emulator-friendly.

Below is a structured overview with an emphasis on _how it actually works_ rather than just the headline features.

* * *

High-level architecture
-----------------------

The chip provides **four audio channels** mixed to a single mono output:

1.  **Tone channel 0** – square wave
2.  **Tone channel 1** – square wave
3.  **Tone channel 2** – square wave
4.  **Noise channel** – pseudo-random or periodic noise

Each channel has:

*   A **frequency (or rate) control**
*   A **4-bit volume control** (16 levels, logarithmic)

There is **no envelope generator**, **no ADSR**, **no waveform selection**, and **no stereo**. Everything is done with simple counters and flip-flops.

* * *

Tone channels (square waves)
----------------------------

### Frequency generation

Each tone channel consists of:

*   A **10-bit frequency divider**
*   A **square-wave flip-flop**

The output frequency is derived from the master clock:

```
f_out = f_clock / (32 × N)
```

Where:

*   `f_clock` is the PSG clock input (e.g. 4 MHz on BBC Micro, 3.579545 MHz on NTSC systems)
*   `N` is the 10-bit divider value (1–1023)

Special case:

*   `N = 0` is treated as `N = 1024` (very low frequency)

The flip-flop toggles every time the divider expires, giving a **50% duty-cycle square wave**.

### Practical consequences

*   Frequencies are **not musically linear** (no equal temperament).
*   Very low values of `N` produce ultrasonic frequencies; real hardware filters them out, emulators often clamp or ignore them.
*   All tone channels are **phase-independent** (no sync between channels).

* * *

Noise channel
-------------

The noise channel is the most interesting part.

### Noise source

It is built around a **15-bit linear feedback shift register (LFSR)**.

Two noise modes:

1.  **White noise** (pseudo-random)
2.  **Periodic noise** (repeating pattern, more “buzzy”)

The feedback polynomial is typically:

```
x^15 + x^14 + 1
```

(This gives a maximal-length 32767-step sequence.)

### Noise rate

The noise channel’s shift rate is selectable:

*   Clock / 512
*   Clock / 1024
*   Clock / 2048
*   Or **derived from tone channel 2**

That last option is important:

*   It allows **pitched noise**, commonly used for drums and explosions.
*   In this mode, every time tone channel 2 toggles, the noise LFSR advances.

### Output

The LFSR’s lowest bit is taken as the noise output, which is then gated by the volume control just like the tone channels.

* * *

Volume control and mixing
-------------------------

Each channel has a **4-bit attenuation register**:

*   0 = loudest
*   15 = silent

The attenuation levels are **logarithmic**, roughly:

*   ~2 dB per step
*   Implemented internally via a resistor ladder

All four channels are **summed internally** to produce a mono output.

In emulation:

*   You usually precompute a 16-entry volume table using a log scale.
*   Muting is literal silence, not “very quiet”.

* * *

Programming model (important for emulation)
-------------------------------------------

### Write-only interface

*   The SN76489 is **write-only**.
*   There are **no readable registers**.
*   The chip maintains an internal “latched register”.

### Data format

Writes are 8 bits and come in two forms:

#### Latch/data byte (bit 7 = 1)

```
1 ccc rrrr
```

*   `ccc` = channel / register select
*   `rrrr` = low bits of data

#### Data-only byte (bit 7 = 0)

```
0 ddddddd
```

*   Continues writing data to the last latched register

This design:

*   Saves pins
*   Makes programming stateful
*   Requires careful emulation of the latch behavior

* * *

Timing and update model
-----------------------

The chip is **fully synchronous** to its clock input:

*   Frequency counters tick continuously
*   Output changes are immediate (no buffering)

However:

*   Register writes are asynchronous relative to audio generation
*   Real hardware produces small “clicks” when registers change mid-cycle

For emulation:

*   You normally update the PSG state at CPU write times
*   Then generate audio samples by advancing the counters over time

* * *

Sound character and limitations
-------------------------------

### Strengths

*   Extremely low CPU overhead
*   Deterministic and stable
*   Distinctive “retro” sound
*   Excellent for:
    *   Chiptunes
    *   Simple melodies
    *   Percussion via noise

### Limitations

*   No envelopes
*   No filters
*   No PCM
*   No stereo
*   Very limited dynamic expressiveness

These constraints are why clever composers relied heavily on:

*   Rapid volume changes (software envelopes)
*   Noise channel tricks
*   Arpeggios (fast pitch cycling)

* * *

Emulation notes (BBC Micro context)
-----------------------------------

Given your emulator work:

*   The BBC Micro uses the **SN76489AN** clocked at **4 MHz**.
*   Audio output is filtered by external analog circuitry, which slightly rounds the square waves.
*   Many BBC programs rely on:
    *   Noise channel synced to tone channel 2
    *   Precise pitch ratios rather than absolute frequencies

A faithful emulation usually includes:

*   Exact divider behavior (including `N = 0`)
*   Correct LFSR polynomial
*   Logarithmic volume table
*   Optional low-pass filtering to soften the sound

* * *

If you like, next we can:

*   Go register-by-register
*   Walk through a minimal reference implementation
*   Discuss timing strategies (per-sample vs event-driven)
*   Or compare SN76489 quirks across different machines (BBC vs Sega vs TI)

Just tell me where you want to go next.

## Prompt:
Beebium has separate backend emulation and front end user interface processes, and a bias towards reflection, introspection and debuggability through APIs. I’m considering exposing the four channels separately, for mixing on the front end. This would allow the volume of each channel to be set by the front end, and even the stereo position to be altered.

## Response:
That’s a very Beebium-ish direction, and it fits well with your emphasis on reflection, introspection, and tooling rather than “just make noise come out”.

Let me break this down along **architecture**, **fidelity implications**, **API shape**, and **practical emulation details**, with a few concrete recommendations.

* * *

1\. Architectural perspective
-----------------------------

Conceptually, the SN76489 already _is_ four independent signal generators plus a trivial summing stage. Treating those four signals as first-class outputs is therefore not a distortion of the model, but rather a **refactoring of the analog domain boundary**.

On real hardware:

```
tone0 ─┐
tone1 ─┼─> resistor ladder → mono out → analog filtering
tone2 ─┼
noise ─┘
```

In Beebium, you are proposing:

```
tone0 ──> API → frontend mixer → speakers
tone1 ──> API → frontend mixer → speakers
tone2 ──> API → frontend mixer → speakers
noise ──> API → frontend mixer → speakers
```

This is a clean separation of concerns:

*   **Backend**: generate _idealised digital signals_
*   **Frontend**: decide how they are presented, mixed, filtered, and spatialised

That aligns extremely well with your stated bias.

* * *

2\. Fidelity and “purism” concerns
----------------------------------

The key question is: _does this break software expectations?_

The short answer: **no, as long as the default behaviour reproduces hardware mixing**.

Important observations:

*   Software written for the SN76489 has **no visibility** into:
    *   Absolute output level
    *   Channel balance
    *   Stereo position
*   The only thing programs rely on is:
    *   Relative loudness between channels
    *   Timing and frequency accuracy

So:

✔ Exposing channels independently is safe  
✔ Allowing the frontend to alter channel gains is safe  
✔ Allowing stereo panning is safe

As long as:

*   The **default frontend mix** reproduces the historical mono mix
*   Channel volume registers are still applied _before_ exposure

Think of the backend output as **post-PSG, pre-mixer**, not “raw register state”.

* * *

3\. What exactly should the backend expose?
-------------------------------------------

This is the most important design choice.

### Recommended abstraction

Expose **four time-varying sample streams**, already including:

*   Frequency generation
*   Square wave / noise logic
*   Volume attenuation (4-bit log scale)
*   Muting when volume = 15

But _not_ including:

*   Summation
*   Stereo placement
*   Global gain
*   Analog filtering (unless you explicitly want a “hardware mode”)

This gives the frontend a clean, powerful surface.

### Concretely

For each channel, the backend provides something like:

```text
ChannelSignal {
    channel_id: TONE0 | TONE1 | TONE2 | NOISE
    sample_rate: e.g. 48 kHz
    samples: float32[] or int16[]
}
```

Or, for introspection/debugging:

```text
ChannelState {
    divider_value
    current_counter
    output_bit
    volume_level
    effective_amplitude
}
```

You can (and probably should) expose **both**:

*   A _signal API_ for audio
*   A _state API_ for tooling, debugging, and visualisation

* * *

4\. Stereo and mixing as a frontend concern
-------------------------------------------

Once channels are separate, the frontend can:

*   Pan tone channels left/right
*   Put noise in the centre
*   Emphasise melody vs bass
*   Mute channels interactively
*   Solo channels for debugging

This is enormously valuable for:

*   Emulator users
*   Music analysis
*   Debugging sound effects
*   Educational tooling

### Default mapping suggestion

Provide a **“BBC faithful” preset**:

| Channel | Pan | Gain |
| --- | --- | --- |
| Tone 0 | Center | 1.0 |
| Tone 1 | Center | 1.0 |
| Tone 2 | Center | 1.0 |
| Noise | Center | 1.0 |

And optionally:

*   A “pseudo-stereo” preset
*   A “debug” preset with exaggerated separation

The key is that the _backend does not care_.

* * *

5\. Timing and determinism considerations
-----------------------------------------

One thing to be careful about:

If you expose _streams_ rather than _events_, you need to ensure:

*   All four channels are **sample-aligned**
*   They advance using the **same master clock**
*   Writes affect channels at the same simulated time

I would strongly recommend:

*   A single PSG clock domain
*   A single sample clock
*   Each channel generating samples in lock-step

Even if the frontend mixes later, the backend must ensure:

> “At sample N, all channels represent the PSG’s output at time T”.

This preserves determinism and makes debugging vastly easier.

* * *

6\. Debuggability and introspection advantages
----------------------------------------------

Your idea shines here.

With separate channels you can:

*   Visualise each waveform independently
*   Show live frequency readouts per channel
*   Display noise LFSR state and mode
*   Detect silence, clipping, or ultrasonic output
*   Implement “break on channel change” style tooling

You could even imagine APIs like:

```http
GET /psg/channels/2/state
```

Returning:

```json
{
  "type": "tone",
  "divider": 174,
  "frequency_hz": 717.6,
  "volume": 6,
  "output_bit": 1
}
```

This is far harder if everything is flattened into a mono stream.

* * *

7\. One caution: volume scaling responsibility
----------------------------------------------

Be explicit about **where volume lives**.

Recommended rule:

*   **PSG volume registers** → backend responsibility
*   **User/UI volume controls** → frontend responsibility

Do not blur these, or you’ll lose clarity in debugging.

* * *

8\. Summary recommendation
--------------------------

Yes — this is a _very_ sound design decision.

In fact, I’d go so far as to say:

> Treating the SN76489 as four independent signal generators with a trivial reference mixer is _more architecturally honest_ than exposing only a mono stream.

As long as you:

*   Preserve a faithful default mix
*   Keep timing exact
*   Apply PSG attenuation before exposure

…you gain flexibility, debuggability, and expressive power at essentially zero emulation risk.

If you want, next we can:

*   Sketch a concrete backend ↔ frontend audio API
*   Discuss sample format choices (float vs int)
*   Talk about exposing analog filtering as an optional post-process
*   Or look at how to model register writes as time-stamped events

Just say where you want to dig next.

## Prompt:
It seems there’s opportunity for highly compressed square wave audio output: phase, amplitude, period. Is it worth it?

## Response:
Short answer: **yes, there is a real opportunity there — but only if you’re clear about _why_ you’re doing it and _where the complexity lives_.**  
Long answer below 🙂

* * *

1\. What you’re really observing
--------------------------------

A tone channel on the SN76489 is not “audio” in the usual sense. It is:

*   A **two-level signal** (±A, or 0/1 × amplitude)
*   With a **constant period** for long stretches
*   And **instantaneous transitions** at known times

So mathematically, each tone channel is fully described by:

*   **Amplitude** (from the volume register)
*   **Period** (derived from the divider)
*   **Phase** (current position in the cycle)

The noise channel is similar, except:

*   The waveform is driven by an **LFSR step rate**
*   The output bit changes at discrete, predictable times

This means the SN76489 is essentially an **event-driven signal generator**, not a continuous one.

* * *

2\. Two models of “audio output”
--------------------------------

### Model A: Sample-based (conventional)

Backend emits:

*   48 kHz float samples per channel

Pros:

*   Simple
*   Universally compatible
*   Easy to mix

Cons:

*   Vastly over-specified
*   Redundant for square waves
*   Harder to introspect semantically

### Model B: Parametric / event-based (what you’re proposing)

Backend emits:

*   “Signal descriptors”:
    *   amplitude
    *   period (or frequency)
    *   phase
*   Plus **change events** when any of these mutate

Pros:

*   Extremely compact
*   Semantically rich
*   Perfect for reflection and debugging
*   Deterministic

Cons:

*   Frontend must _reconstruct_ audio
*   More complex API
*   Noise channel is trickier

* * *

3\. Is it _worth_ it?
---------------------

### From a bandwidth perspective

If your backend and frontend are in separate processes (or even machines):

*   4 channels × 48 kHz × 32 bits ≈ **25 kB/s**
*   Event stream ≈ **dozens of bytes per register write**

So yes, compression is real — but bandwidth is probably **not your bottleneck** unless you’re remote or recording long traces.

* * *

### From a _conceptual_ perspective

This is where it _really_ pays off.

Your compressed representation is:

*   Closer to the **actual hardware model**
*   Naturally supports:
    *   “Show me the current pitch”
    *   “When did this note start?”
    *   “Why is this channel silent?”
*   Ideal for **time-travel debugging**
*   Friendly to deterministic replay

In Beebium terms, this is a **first-class reflective representation**, not a hack.

* * *

4\. The right way to think about it
-----------------------------------

The key insight:

> _Don’t think of this as “compressed audio”. Think of it as “symbolic signal generation with a reference renderer”._

The backend is not producing sound — it is producing a **score**.

The frontend is the synthesiser.

* * *

5\. A practical hybrid architecture (strongly recommended)
----------------------------------------------------------

Rather than choosing _either_ samples _or_ parametrics, consider this:

### Backend exposes two layers

#### 1\. **Event / parametric API** (authoritative)

*   Register writes with timestamps
*   Derived channel state:
    *   period
    *   amplitude
    *   phase
    *   noise mode
*   “Signal change” events

This is what:

*   Debuggers
*   Visualisers
*   Inspectors
*   Record/replay systems  
    consume.

#### 2\. **Optional rendered samples** (convenience)

*   Generated _from the parametric model_
*   Guaranteed to be consistent
*   Can be disabled if frontend wants full control

This lets simple frontends work immediately, while power tools use the richer model.

* * *

6\. Noise channel: the spoiler
------------------------------

Tone channels compress beautifully.

Noise does not — _unless_ you embrace its nature.

Options:

1.  **Expose the LFSR itself**
    *   Current state
    *   Step rate
    *   Mode
    *   Deterministic, perfect
    *   Frontend renders noise
2.  **Expose a “noise clock” + output bit**
    *   Event whenever the bit flips
    *   Still very compact
3.  **Fall back to sample stream for noise only**
    *   Simpler
    *   Slightly asymmetrical API

Given your introspection goals, (1) is very on-brand.

* * *

7\. Phase matters more than people think
----------------------------------------

If you expose:

*   frequency + amplitude  
    but _not_ phase:

You lose:

*   Click-free transitions
*   Faithful reproduction of mid-cycle register writes
*   Exact synchronisation between channels

So if you do this, **phase must be explicit**.

That alone justifies an event-driven model.

* * *

8\. When is it _not_ worth it?
------------------------------

It’s probably _not_ worth it if:

*   Your frontend is a generic audio API wrapper
*   You want zero audio-domain logic outside the backend
*   You don’t care about sound introspection or tooling

But Beebium is very clearly **not** that project.

* * *

9\. Bottom line
---------------

Yes — it’s worth it **if you treat it as a semantic interface, not a compression trick**.

For Beebium specifically, this approach:

*   Matches the real hardware abstraction
*   Dramatically improves debuggability
*   Enables rich visual and analytical tools
*   Keeps audio deterministic and inspectable
*   Still allows faithful sample rendering

If you want, next we can:

*   Design a concrete parametric signal schema
*   Work through phase-accurate transitions
*   Discuss how to schedule events vs audio clocks
*   Compare this approach with how modern chiptune trackers model PSGs

Just say the word.

## Prompt:
I’ll consider this aspect as I review my needs and direction. If I decide to emit sampled audio, rather than symbolic audio, what sample frequency do I need to sound authentic? What about sample precision?

## Response:
Good questions — and they’re the _right_ ones to ask if you decide to stay in the sampled-audio world while still caring about authenticity rather than audiophile excess.

I’ll split this into **sample rate**, **sample precision**, and then a short note on **what actually matters perceptually for SN76489**.

* * *

1\. Sample frequency (sample rate)
----------------------------------

### Absolute minimum (theoretical)

The highest possible tone frequency occurs when the divider is at its minimum:

```
f_out ≈ f_clock / (32 × 1)
```

For a BBC Micro (4 MHz clock):

```
≈ 125 kHz
```

That is _far_ above human hearing, but also far above any sensible output filtering. On real hardware, those ultrasonic tones are heavily attenuated by the analog circuitry and the speaker.

So Nyquist theory applied _literally_ is not helpful here.

* * *

### Practical lower bound

What you actually need is a rate that:

1.  Preserves **timing accuracy of edges**
2.  Avoids **audible aliasing** for in-band frequencies
3.  Allows decent reconstruction of sharp square waves

In practice:

*   **22.05 kHz**
    *   _Borderline_
    *   Audible aliasing in higher notes
    *   Transients sound “sandy”
*   **44.1 kHz**
    *   **Historically accurate enough**
    *   Square waves still sound sharp
    *   Common in classic emulators
*   **48 kHz**
    *   **Excellent default**
    *   Cleaner high end
    *   Plays well with modern audio pipelines

### Recommendation

> **48 kHz is the sweet spot**  
> 44.1 kHz is acceptable, but 48 kHz gives you a little more headroom for sharp edges and filtering.

Going higher (96 kHz, 192 kHz) yields diminishing returns unless you are explicitly modelling analog reconstruction filters.

* * *

2\. Sample precision (bit depth)
--------------------------------

### What the chip actually produces

Internally:

*   Output is **binary** (square wave / noise bit)
*   Amplitude is controlled by a **4-bit logarithmic attenuation**
*   Effective dynamic range is roughly **~20–25 dB**

That’s _tiny_ by modern standards.

* * *

### Integer PCM

*   **8-bit PCM**
    *   Historically used
    *   Noticeable quantisation noise
    *   Can sound gritty in quiet passages
*   **16-bit PCM**
    *   **More than enough**
    *   Quantisation noise far below the PSG’s own limitations
    *   Simple and widely supported
*   **24-bit PCM**
    *   No audible benefit for PSG emulation
    *   Slightly more expensive
    *   Only useful if doing heavy post-processing

### Floating point

*   **32-bit float**
    *   Convenient
    *   Excellent for mixing and gain control
    *   Not “more authentic”, just more flexible

* * *

### Recommendation

If you emit PCM:

*   **16-bit signed integer** is _perfectly authentic_
*   **32-bit float** is _architecturally convenient_

Choose based on pipeline ergonomics, not sound quality.

* * *

3\. The real authenticity killers (and how to avoid them)
---------------------------------------------------------

Sample rate and bit depth matter less than _these_ three things:

### 1\. Edge timing accuracy

If you:

*   Quantise transitions to sample boundaries
*   Or update frequencies only on block boundaries

You will hear:

*   Pitch wobble
*   Clicks
*   Smearing of fast arpeggios

**Solution**:

*   Generate samples by **advancing the PSG clock**, not by naïve oscillator formulas.
*   Or use band-limited step synthesis (BLEP / minBLEP) if you’re fancy.

* * *

### 2\. Aliasing of square waves

Naïve square waves alias _hard_.

Options:

*   Accept it (many emulators do)
*   Add a simple low-pass filter (~12–15 kHz)
*   Or use band-limited synthesis

Even a cheap filter dramatically improves perceived authenticity.

* * *

### 3\. Volume law accuracy

The SN76489’s volume steps are logarithmic.

If you map volume linearly:

*   The chip will sound wrong
*   Especially in fades and envelopes

**This matters more than sample rate.**

* * *

4\. A very solid “authentic” configuration
------------------------------------------

If you want a concrete answer you can act on:

*   **Sample rate**: `48,000 Hz`
*   **Sample format**: `float32` internally, `int16` at output
*   **Channels**: 4 separate streams or 1 mixed stream
*   **Filtering**: optional gentle low-pass
*   **Volume table**: accurate log scale
*   **Timing**: sample-accurate event handling

This will sound _indistinguishable_ from real hardware to virtually all listeners.

* * *

5\. One final, slightly heretical point
---------------------------------------

If your emulator is _too_ clean:

*   Real BBC Micros had:
    *   Slight analog filtering
    *   Imperfect speakers
    *   RF or cheap audio paths

A perfectly band-limited, crystal-clean SN76489 can sound _less_ authentic than a slightly rough one.

So consider:

*   A “hardware realism” toggle
*   Or mild output filtering/noise

* * *

### Bottom line

*   **48 kHz / 16-bit (or float)** is more than sufficient
*   Timing and volume law matter more than resolution
*   Oversampling is optional, not required
*   Authenticity lives in _behaviour_, not specs

If you want, next we can:

*   Look at exact volume tables used by real chips
*   Discuss aliasing strategies (cheap vs correct)
*   Talk about block sizes and latency trade-offs
*   Or compare how existing BBC emulators do this (and where they go wrong)

Just say where you want to go next.

## Prompt:
If there are only five distinct levels, and I’m not doing filtering serverside, why do I need 16-bit precision? Sure 8 bit is sufficient, then four channels pack into one 32-bit word.

## Response:
You’re absolutely right to challenge this — and the short answer is:

> **You don’t need 16-bit precision for correctness.  
> You need it (or float) for _headroom, mixing safety, and ergonomics_, not for representability.**

Let’s unpack this carefully, because your instinct is _mathematically_ sound.

* * *

1\. How many levels are we _really_ talking about?
--------------------------------------------------

For each tone channel at any instant:

*   Output bit: `{0, 1}` (or `{−1, +1}`)
*   Volume: 4-bit attenuation → **16 settings**
*   Effective output levels: **16 non-zero amplitudes + silence**

However:

*   Silence is just amplitude = 0
*   Square wave polarity doubles that

So per channel you have at most:

*   **33 distinct instantaneous values** (including silence)

Even if you normalise to unsigned samples:

*   That’s ~6 bits of information
*   **8-bit PCM is plenty**

You are completely correct on representability.

* * *

2\. Packing four channels into 32 bits
--------------------------------------

If you emit **per-channel samples**, then:

*   4 × 8-bit = 32 bits
*   One word per sample frame
*   Cache-friendly
*   Zero quantisation error
*   Very “Beebium”

This is a _perfectly valid_ and elegant internal format.

If your frontend:

*   Separates channels
*   Applies gains
*   Mixes later

Then there is **no information loss whatsoever** at this stage.

* * *

3\. Why people still default to 16-bit or float
-----------------------------------------------

Not because the SN76489 needs it — but because **pipelines do**.

### 3.1 Summation headroom

If you _sum_ channels server-side:

*   Four channels at max amplitude can overflow 8 bits
*   You’d need:
    *   Saturation logic
    *   Or wider accumulators anyway

But you’ve explicitly said:

> “I’m not doing filtering serverside”

And you’re considering _separate channels_.

So this objection does **not** apply.

* * *

### 3.2 Frontend gain and pan

The moment the frontend applies:

*   Volume sliders
*   Panning
*   Stereo mixing

It will almost certainly convert to:

*   16-bit or float internally anyway

So the _final_ precision is a frontend concern.

* * *

### 3.3 Aliasing and filtering

If you were doing:

*   Band-limiting
*   Low-pass filters
*   BLEPs

Then 8-bit would be unpleasant.

But again:

> You are _not_ doing filtering serverside.

So this also doesn’t apply.

* * *

4\. Where 8-bit _can_ bite you (subtle cases)
---------------------------------------------

These are edge cases, not showstoppers.

### 4.1 Logarithmic volume law fidelity

If you precompute a log-volume table and map it into 8 bits:

*   The _smallest_ volumes may collapse to 0 or 1
*   You lose the very quiet tail

But:

*   The SN76489’s quietest levels are _barely audible anyway_
*   On real hardware they’re often lost in noise

So this is arguably _more authentic_, not less.

* * *

### 4.2 Click energy on transitions

Square-wave edges are discontinuities.

With 8-bit samples:

*   The step size is larger
*   Click energy is marginally higher

But:

*   Real hardware produces sharp edges too
*   You’re not band-limiting

So again: authenticity is not harmed.

* * *

5\. Conceptual cleanliness (your strongest argument)
----------------------------------------------------

Your packing idea is actually **conceptually correct**:

*   Each channel is a _symbolic signal with a small alphabet_
*   Emitting 32-bit float samples would be _semantic inflation_

A packed 32-bit word per frame:

```
[ noise | tone2 | tone1 | tone0 ]
   8b      8b      8b      8b
```

is:

*   Minimal
*   Deterministic
*   Lossless
*   Easy to inspect
*   Easy to log and replay

This is very much in line with:

> “reflection, introspection and debuggability through APIs”

* * *

6\. A pragmatic recommendation
------------------------------

I would suggest:

### Backend internal representation

*   **Per-channel signed 8-bit samples**
*   Or packed 32-bit words if that suits your IPC layer

### Backend guarantees

*   Exact PSG timing
*   Exact volume law
*   No clipping within a channel
*   Sample-aligned channels

### Frontend responsibilities

*   Convert to wider format if needed
*   Apply gains / panning
*   Optional filtering
*   Final output format

This gives you:

*   Maximum efficiency
*   Maximum transparency
*   No premature precision

* * *

7\. One optional refinement
---------------------------

If you want _just a little_ extra safety:

*   Use **signed 8-bit centred at 0** (−128…127)
*   Reserve one bit-pattern for “silence” if useful
*   Document the exact amplitude mapping

Or:

*   Use **signed 9 or 10 bits packed** if you want perfect log-law fidelity

But honestly: **plain int8 is fine**.

* * *

8\. Bottom line
---------------

You are not missing anything.

*   16-bit precision is **not required**
*   8-bit per channel is **lossless for SN76489 output**
*   Packing four channels into 32 bits is elegant and justified
*   Any widening should happen _only when needed_

This is one of those rare cases where the “obvious” modern choice is actually worse than the minimal one.

If you want, next we can:

*   Define an exact 8-bit volume table
*   Specify the packed sample format formally
*   Discuss IPC transport (shared memory vs ring buffer)
*   Or explore hybrid symbolic + 8-bit sample emission

Just say the word.

## Prompt:
Yes, 8-bit volume table.

## Response:
Great — let’s pin this down precisely.

Below is a **practical, authentic 8-bit volume table** for the SN76489, with explanation of where it comes from and why it works well in your architecture.

* * *

1\. What we’re modelling
------------------------

Facts we want to preserve:

*   16 volume settings (0–15)
*   Logarithmic attenuation
*   ≈ **2 dB per step**
*   Volume 15 = silence
*   Output is a _binary waveform scaled by volume_

The SN76489 attenuation is usually described as:

```
attenuation(dB) ≈ 2 × volume
```

So:

*   volume 0 → 0 dB
*   volume 15 → ~30 dB attenuation (effectively silent)

* * *

2\. Mapping to signed 8-bit samples
-----------------------------------

We’ll use:

*   **Signed 8-bit samples**: −127 … +127
*   Square wave polarity: ±amplitude
*   Silence = 0

Let:

*   `A_max = 127`
*   Each step attenuates by a factor of `10^(−2/20) ≈ 0.794328`

* * *

3\. Reference 8-bit volume table
--------------------------------

This table is widely used (or extremely close to what many high-quality emulators use), rounded to integers and tuned for 8-bit output:

```text
Volume  Attenuation   Amplitude (int8)
--------------------------------------
0       0 dB          127
1       -2 dB         101
2       -4 dB         80
3       -6 dB         64
4       -8 dB         51
5       -10 dB        40
6       -12 dB        32
7       -14 dB        25
8       -16 dB        20
9       -18 dB        16
10      -20 dB        13
11      -22 dB        10
12      -24 dB        8
13      -26 dB        6
14      -28 dB        5
15      silence       0
```

This gives you:

*   Clean powers-of-two-ish values
*   No wasted dynamic range
*   Very stable behaviour at low volumes
*   No need for dithering

* * *

4\. How to use it
-----------------

### Tone channels

```c
int8_t output = (square_bit ? +amp : -amp);
```

Where:

```c
amp = volume_table[volume_register];
```

### Noise channel

Same rule:

*   Noise bit = 0 → −amp
*   Noise bit = 1 → +amp

Silence naturally collapses to zero.

* * *

5\. Why this table is _good enough_
-----------------------------------

*   The **quietest non-zero levels are barely audible** on real hardware
*   The BBC Micro audio path loses even more dynamic range
*   Integer rounding at low levels is _authentic_, not harmful
*   The ear is far more sensitive to **relative loudness** than exact dB values

This table preserves:

*   Envelope shape
*   Fade behaviour
*   Relative balance between channels

Which is what actually matters.

* * *

6\. Optional: exact formula (for documentation)
-----------------------------------------------

If you want the table to be _derived_ rather than magic:

```c
amp[v] = round(127 * pow(10.0, -(2.0 * v) / 20.0));
amp[15] = 0;
```

But I would **bake the table**, not compute it at runtime:

*   Faster
*   Deterministic
*   Easier to inspect and test

* * *

7\. One Beebium-friendly refinement
-----------------------------------

Because you care about introspection:

*   Expose the **volume index** separately from amplitude
*   Document that amplitude is _derived_, not state

This lets tools say:

> “Channel 1 at volume 6 (−12 dB)”

rather than:

> “Channel 1 amplitude = 32”

Much more meaningful.

* * *

8\. Summary
-----------

*   8-bit signed amplitude is **sufficient and correct**
*   The table above is faithful and robust
*   Silence maps cleanly to zero
*   Square-wave polarity does the rest
*   Packing four channels into one 32-bit word remains lossless

If you want, next we can:

*   Tune the table specifically for BBC Micro output circuitry
*   Compare this table with Sega / TI variants
*   Define a canonical Beebium PSG sample format
*   Or discuss how to time-align volume changes sample-accurately

Just tell me where you want to go next.

## Prompt:
Yes, talk to me about the time domain, and how to avoid obvious aliasing.

## Response:
This is the right place to focus now. With square waves, **time-domain behaviour dominates perceived quality** far more than bit depth or even sample rate.

I’ll structure this around:

1.  **What causes aliasing in a PSG**
2.  **What you can and cannot fix without filtering**
3.  **Time-domain strategies that work**
4.  **A minimal, honest solution**
5.  **Where to draw the line for Beebium**

* * *

1\. Where aliasing actually comes from
--------------------------------------

In an SN76489, aliasing is _not_ primarily about high frequencies per se. It comes from **sampling discontinuities at the wrong times**.

Two sources dominate:

### A. Square-wave edges

A square wave is an infinite-bandwidth signal.

If you:

*   Flip the output bit only at sample boundaries
*   Or use a naïve oscillator (`phase += freq / fs`)

Then:

*   Edge timing is quantised
*   High harmonics fold back into the audible band
*   You hear “buzz” or “sand” on higher notes

* * *

### B. Mid-cycle register writes

On real hardware:

*   Frequency changes
*   Volume changes
*   Noise mode changes

…all take effect **immediately**, not at waveform boundaries.

If you defer these changes to:

*   Next sample
*   Next block
*   Next half-cycle

You create:

*   Phase jumps
*   DC offsets
*   Audible clicks

This is _time-domain aliasing_, not frequency aliasing.

* * *

2\. What you _cannot_ avoid without filtering
---------------------------------------------

Let’s be clear:

If you emit:

*   Raw square waves
*   With no band-limiting
*   At 44.1 or 48 kHz

Then:

*   There will be aliasing
*   Always
*   On every square wave

This is _authentic_. Real hardware relies on analog filtering and speakers to smooth it.

So your goal is **avoid obvious digital artefacts**, not eliminate aliasing entirely.

* * *

3\. The single most important rule
----------------------------------

> **Drive audio from the PSG clock, not from the sample clock.**

This one decision solves ~80% of problems.

### Bad approach (common)

```text
for each sample:
    phase += freq / fs
    if phase >= 1.0: toggle
```

Problems:

*   Phase drift
*   Inexact frequencies
*   Edge jitter

* * *

### Good approach (PSG-accurate)

*   Maintain **counters in PSG clock ticks**
*   Maintain **time to next edge**
*   Generate samples by _integrating time_

Conceptually:

```text
while generating sample:
    advance PSG clock until sample boundary
    apply any toggles that occur
    output current level
```

This guarantees:

*   Exact periods
*   Exact phase
*   Correct mid-cycle updates

* * *

4\. Sample-accurate edge placement (key insight)
------------------------------------------------

You _do not_ need BLEPs or fancy DSP to avoid the worst aliasing.

You need this:

> **Edges must occur at the correct fractional position inside the sample.**

Two simple strategies:

* * *

### Strategy A: Zero-order hold (simplest)

*   Output one value per sample
*   Edge occurs “somewhere inside” the sample
*   You just output the post-edge value

This:

*   Is fast
*   Is authentic
*   Aliases, but _musically acceptable_

Most classic emulators do exactly this.

* * *

### Strategy B: Two-level averaging (better, still cheap)

If an edge occurs at fraction `t` of the sample:

```text
sample = (t * old_level) + ((1 - t) * new_level)
```

This:

*   Preserves average energy
*   Dramatically reduces harshness
*   Costs almost nothing

This is a **time-domain anti-aliasing trick**, not filtering.

It works _especially well_ for square waves.

* * *

5\. Handling register writes correctly
--------------------------------------

This is where many emulators go wrong.

### Correct model

*   Register writes are timestamped in **CPU cycles**
*   CPU cycles map to PSG clock cycles
*   Changes take effect **immediately**

So during one audio sample:

*   Frequency may change
*   Volume may change
*   Noise mode may change

Your generator must:

*   Split the sample if needed
*   Or at least account for the timing

Again, averaging helps.

* * *

6\. Noise channel specifics
---------------------------

Noise is _much_ less sensitive to aliasing:

*   It already has a wide spectrum
*   Aliasing sounds like… noise

But timing still matters:

*   The LFSR must step at exact times
*   Especially when clocked from tone channel 2

Treat noise bit flips exactly like square-wave edges.

* * *

7\. Minimal, honest solution (recommended)
------------------------------------------

Given your stated goals, I’d suggest:

### Backend

*   48 kHz output
*   Signed 8-bit per channel
*   PSG-clock-driven counters
*   Edge-accurate timing
*   Optional two-level averaging within a sample
*   No filtering

### Frontend (optional)

*   Gentle low-pass if desired
*   Stereo placement
*   Mixing

This gives you:

*   Exact pitch
*   Correct envelopes
*   No obvious digital harshness
*   High debuggability

* * *

8\. What _not_ to do
--------------------

Avoid:

*   Phase accumulators tied to sample rate
*   Block-based register updates
*   “Fixing” aliasing with heavy filters
*   Oversampling unless you really want complexity

These tend to:

*   Hide bugs
*   Reduce introspection
*   Move behaviour away from hardware

* * *

9\. A useful mental model
-------------------------

Think of the SN76489 as producing:

> **A piecewise-constant signal with discontinuities at known times**

Your job is not to _smooth_ it, but to:

*   Place those discontinuities correctly in time
*   Preserve their average energy per sample

Do that, and the sound “locks in”.

* * *

10\. Bottom line
----------------

*   Aliasing is inevitable — and acceptable
*   **Timing correctness beats DSP cleverness**
*   PSG-clock-driven generation is the big win
*   Simple intra-sample averaging removes the worst artefacts
*   Anything more is optional polish

If you want next steps, we can:

*   Walk through a concrete sample-generation loop
*   Design an event-driven PSG scheduler
*   Compare naïve vs accurate waveforms visually
*   Decide how much of this belongs backend vs frontend

Just say where you’d like to go next.



---
Powered by [ChatGPT Exporter](https://www.chatgptexporter.com)