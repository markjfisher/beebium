# Sound Subsystem

## Overview

Beebium implements BBC Micro audio through a combination of:
1. **SN76489 sound chip emulation** - 3 tone channels + 1 noise channel
2. **Flexible N x 32-bit audio format** - supports future expansion (speech, 1 MHz bus audio)
3. **gRPC AudioService** - streaming and introspection for frontends

## Architecture

```
┌─────────────────────────────────────────────────────────────┐
│                      Beebium Core                           │
├─────────────────────────────────────────────────────────────┤
│  Machine::step()                                            │
│    ├─> System VIA Port A writes                            │
│    │     └─> Addressable Latch bit 0 (sound_write_enabled) │
│    │           └─> Sn76489::write(data)                    │
│    └─> Sn76489::tick() [called at 2 MHz CPU rate]          │
│          └─> Phase accumulator derives 250 kHz internal    │
│                └─> AudioBuffer::push(AudioSample)          │
├─────────────────────────────────────────────────────────────┤
│  AudioBuffer (circular SPSC queue, 48000 samples)           │
└─────────────────────────────────────────────────────────────┘
                          │
                          │ gRPC stream
                          ▼
┌─────────────────────────────────────────────────────────────┐
│                   AudioService (gRPC)                       │
├─────────────────────────────────────────────────────────────┤
│  SubscribeAudio() → stream AudioChunk                       │
│  GetAudioFormat() → metadata (encoding, channel names)      │
│  GetChannelStates() → introspection (freq, vol, LFSR)      │
└─────────────────────────────────────────────────────────────┘
```

## SN76489 Sound Chip

### Hardware Characteristics

- **Input clock**: 4 MHz (from Video ULA, directly to pin 14)
- **Internal clock**: 250 kHz (4 MHz ÷ 16 internal divider)
- **Output sample rate**: 48 kHz (emulator output)
- **Channels**: 3 tone generators + 1 noise generator

**Emulator note**: The emulator calls `tick()` at 2 MHz (CPU clock rate) and uses a
phase accumulator to derive the same 250 kHz internal clock (2 MHz ÷ 8 = 250 kHz).

### Register Protocol

The chip uses a latch/data byte protocol:

**Latch byte** (bit 7 = 1): `%1RRRdddd`
- RRR (bits 4-6): Register select (0-7)
- dddd (bits 0-3): Data low nibble

**Data byte** (bit 7 = 0): `%0Ddddddd`
- dddddd (bits 0-5): Data high 6 bits (tone frequency only)

### Register Map

| Register | Function |
|----------|----------|
| 0 | Tone 0 frequency (10-bit) |
| 1 | Tone 0 volume (4-bit, 0=max, 15=silent) |
| 2 | Tone 1 frequency |
| 3 | Tone 1 volume |
| 4 | Tone 2 frequency |
| 5 | Tone 2 volume |
| 6 | Noise control (rate + mode) |
| 7 | Noise volume |

### BBC BASIC Channel Mapping

The MOS inverts tone channel numbers:

| BBC BASIC Channel | SN76489 Register | Description |
|-------------------|------------------|-------------|
| 0 | 6, 7 | Noise channel |
| 1 | 4, 5 | Tone 2 (highest numbered) |
| 2 | 2, 3 | Tone 1 |
| 3 | 0, 1 | Tone 0 (lowest numbered) |

This mapping is defined in the MOS soundParameterTable at address $EB40.

### Frequency Calculation

```
frequency_hz = clock_hz / (32 × divider)
```

Where:
- `clock_hz` = 4,000,000 (4 MHz)
- `divider` = 10-bit value from register (0 treated as 1024)
- Range: ~122 Hz to 125 kHz

### Volume Table

4-bit logarithmic attenuation (~2 dB per step):

| Register Value | Attenuation | Amplitude (8-bit) |
|----------------|-------------|-------------------|
| 0 | 0 dB | 127 |
| 1 | -2 dB | 101 |
| 2 | -4 dB | 80 |
| ... | ... | ... |
| 14 | -28 dB | 5 |
| 15 | Silence | 0 |

### Noise Generator

- **15-bit LFSR** with taps at bits 0 and 1
- **White noise**: XOR feedback, period 32767
- **Periodic noise**: Simple rotation, period 15

Noise rate select (bits 0-1 of register 6):
- 0: N/512 (~488 Hz)
- 1: N/1024 (~244 Hz)
- 2: N/2048 (~122 Hz)
- 3: Clocked from Tone 2 output

### DC Bias Behavior

The SN76489 has unusual DC bias characteristics that are important for advanced audio techniques like sampled sound playback.

**Key observations from real hardware** (see [Chris Evans' analysis](https://scarybeastsecurity.blogspot.com/2020/06/sampled-sound-1980s-style-from-sn76489.html)):

- Output is centered around ~0.8V, not 0V
- Silent channels output a constant ~0.8V
- Max volume channels oscillate with ~0.8V peak-to-peak swing
- The mid-point voltage varies with volume setting

**Why this matters:**

Sampled sound playback on the BBC Micro works by rapidly modulating volume to encode PCM data. The varying mid-point voltage at each volume level allows PCM waveforms to emerge from a high-frequency carrier (125 kHz). Downstream circuitry (LM386 amplifier) filters out the carrier, leaving the modulated audio.

**Beebium implementation:**

Beebium exposes DC bias as metadata (not baked into samples) to enable accurate reconstruction when needed:

```cpp
struct ToneChannelState {
    // ... standard fields ...
    float dc_bias_v;   // Mid-point voltage (~0.8V)
    float peak_v;      // Voltage when output_bit=1
    float trough_v;    // Voltage when output_bit=0
};
```

The swing follows a 2dB attenuation curve matching the volume table:

| Volume | Swing (V) | DC Bias (V) |
|--------|-----------|-------------|
| 0 (max) | 0.800 | 0.800 |
| 5 | 0.253 | 0.800 |
| 10 | 0.080 | 0.800 |
| 15 (silent) | 0.000 | 0.800 |

**Frontend usage example:**

```python
# Reconstruct analog voltage from sample + DC bias metadata
for sample in audio_chunk:
    tone0_amplitude = unpack_int8(sample.sources[0], 0)

    # Get DC bias from channel state
    state = audio_service.get_channel_states()

    # Reconstruct analog voltage
    if tone0_amplitude > 0:
        voltage = state.channels[0].peak_voltage
    else:
        voltage = state.channels[0].trough_voltage

    # Apply high-pass filter to remove DC, low-pass for carrier removal
    filtered_output = process(voltage)
```

## Audio Sample Format

Each sample contains N x 32-bit source fields:

```cpp
struct AudioSample {
    static constexpr size_t MAX_SOURCES = 4;
    uint32_t sources[MAX_SOURCES];
};
```

### SN76489 Source (index 0)

Encoding: 4 x 8-bit signed channels

```
Byte order (big-endian within 32-bit field):
[Tone0 | Tone1 | Tone2 | Noise]
  MSB                     LSB
```

Each channel is an 8-bit signed amplitude (-127 to +127).

### Future Sources (reserved)

- Index 1: Speech synthesizer (TMS5220)
- Index 2: 1 MHz bus audio (Music 5000)
- Index 3: Reserved

## gRPC AudioService

### SubscribeAudio

Streams audio chunks at 48 kHz:

```protobuf
rpc SubscribeAudio(SubscribeAudioRequest) returns (stream AudioChunk);

message AudioChunk {
    uint64 sequence = 1;      // For drop detection
    uint32 sample_count = 2;  // Samples in this chunk
    bytes samples = 4;        // Packed sample data
}
```

### GetAudioFormat

Returns metadata for interpreting samples:

```protobuf
rpc GetAudioFormat(GetAudioFormatRequest) returns (AudioFormat);

message AudioFormat {
    uint32 sample_rate = 1;           // 48000
    uint32 source_count = 2;          // 4
    repeated AudioSource sources = 3; // Per-source metadata
    repeated ChannelGroup groups = 4; // UI grouping hints
}
```

### GetChannelStates

Returns current chip state for introspection:

```protobuf
rpc GetChannelStates(GetChannelStatesRequest) returns (ChannelStatesResponse);

message ChannelState {
    uint32 channel_id = 1;
    uint32 frequency_divider = 3;
    uint32 volume = 6;
    float frequency_hz = 7;
    int32 amplitude = 8;
    // Noise-specific fields for channel 3
}
```

## Frontend Integration

### Sample Unpacking (Python example)

```python
def unpack_samples(chunk, format):
    samples = chunk.samples
    source_count = format.source_count

    for i in range(chunk.sample_count):
        offset = i * source_count * 4

        # Read SN76489 source (index 0), little-endian
        sn76489_data = struct.unpack_from('<I', samples, offset)[0]

        # Unpack 4 channels (stored big-endian in the 32-bit field)
        tone0 = ctypes.c_int8((sn76489_data >> 24) & 0xFF).value
        tone1 = ctypes.c_int8((sn76489_data >> 16) & 0xFF).value
        tone2 = ctypes.c_int8((sn76489_data >> 8) & 0xFF).value
        noise = ctypes.c_int8(sn76489_data & 0xFF).value

        yield (tone0, tone1, tone2, noise)
```

### Per-Channel Volume/Panning

Frontends can apply per-channel processing before mixing:

```python
for tone0, tone1, tone2, noise in unpack_samples(chunk, format):
    left = right = 0

    if not muted[0]:
        left += tone0 * volume[0] * (1 - pan[0])
        right += tone0 * volume[0] * (1 + pan[0])

    # ... repeat for other channels

    output(left, right)
```

## Files

| File | Description |
|------|-------------|
| `src/core/include/beebium/devices/Sn76489.hpp` | SN76489 class declaration |
| `src/core/src/Sn76489.cpp` | SN76489 implementation |
| `src/core/include/beebium/AudioBuffer.hpp` | Circular buffer for samples |
| `src/service/proto/audio.proto` | gRPC service definitions |
| `src/service/include/beebium/service/AudioService.hpp` | Service implementation |
| `tests/test_sn76489.cpp` | Unit tests |
| `tests/test_sound_basic.cpp` | BBC BASIC integration tests |
| `tests/test_sound_via.cpp` | Direct VIA integration tests |
| `tests/test_grpc_audio.cpp` | gRPC service tests |

## Useful References

- [SN76489 chip details](http://www.zeridajh.org/articles/me_sn76489_sound_chip_details/index.html)
- [SN76489 Development](https://www.smspower.org/Development/SN76489)
- [SN76489 bus side behavior](https://stardot.org.uk/forums/viewtopic.php?t=30838)
- [Sampled sound on the SN76489](https://stardot.org.uk/forums/viewtopic.php?t=17537) - DC bias and volume modulation for PCM playback
- [Sampled sound 1980s style](https://scarybeastsecurity.blogspot.com/2020/06/sampled-sound-1980s-style-from-sn76489.html) - Chris Evans' hardware measurements
- [MOS 1.20 Reassembly](https://tobylobster.github.io/mos/mos/S-s16.html)
