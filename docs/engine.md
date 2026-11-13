# Engine

The C++ audio engine lives entirely in `engine/src/`. It is compiled as a static library
(`tl_engine`) and has no platform dependencies other than the `AudioIO` interface it
defines. All state transitions happen on the RT audio thread; the UI thread only reads
snapshots and pushes commands.

## Sample data model (`engine/src/sample.h`)

A `Sample` object holds the raw PCM data for one recorded loop and is owned by the engine's
internal `samples` vector (a `std::vector<std::unique_ptr<Sample>>`).

**Immutable after recording completes**

| Field | Type | Meaning |
|-------|------|---------|
| `id` | `SampleId` (`uint64_t`) | Monotonically increasing, assigned at allocation |
| `recorded_length_frames` | `Frames` (`uint32_t`) | PCM length after bar quantization; never changes post-record |
| `recorded_offset_in_bar` | `Frames` | Bar-relative playback position; `recorded_start_total mod L` |
| `recorded_start_total` | `int64_t` | Absolute timeline position where recording started (latency-compensated); multi-bar playback anchor |
| `pcm` | `std::vector<float>` | Interleaved stereo samples (shrunk to recorded length at StopRecording) |
| `is_master_loop` | `bool` | True for the first recorded loop; affects play-head init and LAT-shift exclusion |
| `env` | `float[kEnvBins]` | 32-bin peak envelope computed once post-record (UI waveform thumbnail) |

**Mutable (commands change these at any time)**

| Field | Type | Meaning |
|-------|------|---------|
| `bank` | `BankId` | Which bank the sample belongs to |
| `volume` | `float` | Per-sample gain, 0..1 |
| `reverse` | `bool` | Playback direction; handled by `read_sample_lerp` index inversion |
| `speed` | `Speed` | Half (0.5×), Normal (1×), Double (2×) |
| `enabled` | `bool` | LOOP mode: whether the loop is active |
| `playing` | `bool` | LOOP: audible this bar iteration; ONE-SHOT: currently firing |
| `repeat` | `bool` | Sub-bar LOOP only: tile the sample across the full bar instead of playing once |
| `start_offset_frames` | `int32_t` | Rotates PCM playback position; can be negative |
| `position_x/y` | `float` | Normalised position within the bank body (0..1 each axis) |
| `one_shot_play_pos` | `Frames` | ONE-SHOT cursor; advances each block, resets to 0 on trigger |

Non-destructive guarantee: `MoveSample` changes `bank`, `position_x`, `position_y` only.
Mode changes (`SetBankMode`) reset `enabled`, `playing`, `one_shot_play_pos` but leave
the PCM and `recorded_offset_in_bar` untouched.

## Recording flow

### StartRecording (`CommandType::StartRecording`)

Ignored when the engine already holds `kMaxSamples` (64) samples — the UI grid cannot
show more. Otherwise allocates a new `Sample` with capacity `kMaxRecFrames` (60 s at
48 kHz = 2,880,000 frames) and sets `state.focused_bank` to the target bank (CAPTURE
targets the last-touched bank). Position x/y are taken from the `Command.fx/fy` fields
(normalised bank coordinates).

If the recording reaches the 60 s capacity, the engine auto-applies `StopRecording`
instead of silently freezing the duration counter.

### CancelRecording (`CommandType::CancelRecording`)

Discards the active recording without creating a sample. The UI sends this when a
gesture that started a recording on press-down turns out to be a page swipe.

If a master bar length `L` already exists, the recorded offset is computed with latency
compensation:

```cpp
Frames adjusted = (global_play_head + L - (latency % L)) % L;
active_recording->recorded_offset_in_bar = adjusted;
```

where `latency = round_trip_latency_frames` (auto-detected; see Latency Compensation).
`extra_latency_frames` is applied later at mix time, not here.

### During recording

Every audio callback appends `frame_count` frames of mic input to
`active_recording->pcm` via `Sample::append(in, frame_count)`. The engine also writes the
same input into the rolling buffer for `CaptureLastLoop`.

### StopRecording (`CommandType::StopRecording`)

The loop is exactly the span the user bounded: recording starts when the box is
created (press-down) and ends at the stop press. There is NO content trimming —
leading silence is performance space, and earlier onset-detection trims (leading
silence / loudest onset / trailing onset) moved the loop boundaries away from the
user's gestures and were removed.

The stop sequence applies four post-processing steps in order:

#### 1. Bar-grid length quantization (non-master, longer than a bar)

Playback of multi-bar loops cycles in whole bars, so a 3.9-bar take would wrap
mid-phrase (or leave a silence gap) at the seam. The recorded length is snapped to
the nearest whole bar: `target = round(len / L) * L` — the overhang is truncated, or
the tail is padded with silence (with a short fade at the content edge). Trimmed
audio is never restored, so the stop-click stays gone. Sub-bar takes are untouched —
they already play as a per-bar window.

#### 2. Edge fades

```cpp
constexpr Frames kFadeBase = 96;  // 2 ms at 48 kHz
Frames fade_len = std::min(kFadeBase, rec.recorded_length_frames / 4);
```

A linear ramp-up is applied to the first `fade_len` frames and a ramp-down to the last
`fade_len` frames. This eliminates click/pop at playback start and at the loop-wrap
boundary. Capped to 25% of the recording so short samples are not dominated by the fades.

#### 3. Shrink + envelope

The PCM vector is shrunk from the 60 s preallocation (~23 MB) to the actual recorded
length, and a `kEnvBins`-bin (32) peak envelope is computed for the UI waveform
thumbnail (`Sample::compute_env`). Both are one-off RT-thread allocations in the same
accepted class as the trims.

#### 4. Master bar / play-head init

If this is the first recording (`master_bar_frames == 0`), the engine:
- Marks the sample as `is_master_loop = true`
- Sets `master_bar_frames = recorded_length_frames`
- Resizes the rolling buffer to match `L`
- Resets `global_play_head` to 0 — the stop press IS the loop boundary, so playback
  begins immediately at the top of the loop

If the recording is too short (< `kMinRecordingMs` = 200 ms), it is silently discarded.

## Latency compensation

### Auto-detected round-trip latency

After the miniaudio device initialises, `MiniaudioIO::start` computes:

```cpp
int input_frames  = device_.capture.internalPeriodSizeInFrames
                  * device_.capture.internalPeriods;
int output_frames = device_.playback.internalPeriodSizeInFrames
                  * device_.playback.internalPeriods;
int round_trip = input_frames + output_frames + device_extra;  // device_extra: see below
engine.set_round_trip_latency_frames(round_trip);
```

On macOS the CoreAudio device latency + safety offset for both directions is added on
top (`coreaudio_device_latency_frames` in `audio_io_miniaudio.cpp`) — the ADC/DAC and
driver add several ms that the buffer sizes alone miss. The actual measured value is
printed to stderr on startup.

### User-tunable extra latency

`extra_latency_frames` (default 0) is a by-ear knob for residual latency (speaker/air
acoustics) that can't be measured — the `round_trip` compensation covers the measured
chain: miniaudio buffer sizes plus, on macOS, the CoreAudio device latency + safety
offsets for both directions. The user adjusts it live with the ±2 ms buttons in
BottomBar. The value is signed; positive plays non-master samples EARLIER in the bar
(compensates lag), negative later.

### Where each component is applied

**At StartRecording** (non-master recordings):
```
recorded_offset_in_bar = (play_head + L - round_trip_latency % L) % L
```
This shifts the recording's grid position backward by the round-trip latency so playback
lands in phase with what the user heard.

**At mix time** (in `mixer.cpp`):
```cpp
int64_t shifted = int64_t(offset);
if (!s.is_master_loop) shifted -= int64_t(extra_latency_frames);
offset = Frames(((shifted % L) + L) % L);
```
Applying `extra_latency` at mix time — rather than baking it into `recorded_offset_in_bar`
at record time — means the user can dial the LAT knob live and all existing recordings
shift together without re-recording. The result is always normalised into
`[0, bar_frames)` because `SetMasterBarFrames` can shrink the bar below a previously
recorded offset.

**At master StopRecording** (first loop only): `global_play_head` resets to 0 — the
master loop plays back immediately from its start. No latency shift is applied to the
master; the LAT knob shifts every non-master sample against it.

## Mixer (`engine/src/mixer.cpp`)

`mix(samples, banks, bar_frames, play_head, frame_count, out, extra_latency_frames)`
is called once per audio callback after commands are applied. The output buffer is
pre-zeroed by the caller.

`play_head` is the engine's UNWRAPPED `uint64_t` total-frame counter (frames since the
master timeline started) — the mixer derives the bar position (`% bar_frames`) and the
multi-bar iteration position (`% loop_len`) from it. It must not be pre-wrapped to the
bar: an earlier version wrapped it mod `bar_frames` each callback, which made every
multi-bar sample repeat its first bar forever.

### Solo / mute logic

If any bank has `soloed = true`, only soloed banks contribute to output (all others are
silenced regardless of their mute flag). If no bank is soloed, `muted` banks are skipped.

### ONE-SHOT samples

When `bank.mode == OneShot` and `s.playing == true`, the sample plays from
`one_shot_play_pos` advancing by `speed_ratio(s.speed)` per output frame via
`read_sample_lerp`. When `one_shot_play_pos >= recorded_length_frames` the mixer sets
`s.playing = false` (in-place mutation; the RT thread is the sole writer).

### LOOP samples — three branches

After computing `offset` (normalised, with the LAT shift for non-master samples), the
mixer computes the sample's effective play length in OUTPUT frames:

```cpp
Frames play_len = Frames(float(sample_len) / speed_ratio(s.speed));
```

At 2× speed a sample finishes in half its recorded length; at ½× it takes double.
All three branches size their playback window from `play_len`, and all three map an
output-frame position to a PCM read position through the shared helper:

```cpp
// pcm_position(local, speed, start_offset, sample_len):
pos = positive_mod(local * speed + start_offset, sample_len)
```

so `start_offset_frames` rotates within the PCM consistently everywhere.

Branch choice is based on `s.repeat` and `sample_len` vs `bar_frames`:

#### Branch 1: Repeat

`tile_len = min(play_len, bar_frames)`. The sample tiles continuously across the bar
starting from `offset` — at 2× speed it tiles twice per bar; at ½ speed the tile is
clamped to the bar.

#### Branch 2: Sub-bar non-repeat (`sample_len <= bar_frames`)

The sample plays once per bar in the window `[offset, offset + min(play_len, bar_frames))`
with wrap handling when the window straddles the bar boundary.

#### Branch 3: Multi-bar non-repeat (`sample_len > bar_frames`)

`loop_len = nearest_multiple_of_L(play_len, bar_frames)` (minimum 2L when `play_len > L`).
The `global_play_head` is used modulo `loop_len`, and content is anchored at
`recorded_start_total % loop_len` (minus the LAT shift for non-master samples) — the
overdub plays back at the exact grid position it was performed at. Output frames past
`play_len` (trailing padding within the iteration) are silent.

### Linear interpolation read

`read_sample_lerp(s, position, channel)` computes a fractional-frame read:

```cpp
Frames i0 = Frames(position);
Frames i1 = min(i0 + 1, len - 1);
float frac = position - float(i0);
return pcm[i0] + (pcm[i1] - pcm[i0]) * frac;
```

If `s.reverse` is true, `position` is reflected: `position = (len - 1) - position` before
the lerp. This is applied inside `read_sample_lerp` so all three mixer branches get
reverse for free.

### Per-bank gain

Final gain = `s.volume * b.volume`. Both are in [0, 1].

## Rolling buffer (`engine/src/rolling_buffer.h`)

A continuous ring buffer that always captures the last `L` frames of mic input. Written
every callback before the active recording captures its copy. `CaptureLastLoop` calls
`rolling.read_last(buf, L)` to snapshot the ring without stopping the stream. Before the
master bar is set, the buffer defaults to 8 s (`kFallbackBufferFrames = kSampleRate * 8`).

## BPM derivation (`engine/src/bpm.h`)

```
bpm = beats_per_bar * 60.0 / (master_bar_frames / sample_rate)
```

`beats_per_bar` is user-settable (2, 4, or 8 via `SetBeatsPerBar`). The ±0.1 BPM buttons
in BottomBar work in reverse: they compute a new `master_bar_frames` from the target BPM
and submit `SetMasterBarFrames`.

## Metronome

`SetMetronome(bvalue)` toggles a click on every beat of the master bar (only while
`master_playing` and a bar exists). The click is a short sine burst with exponential
decay (~30 ms), synthesised in `process()` after the mix — 1600 Hz on the downbeat,
1050 Hz on other beats. No allocation; three scalar members hold the synth state.

## Input peak

Every callback updates `state.input_peak = max(buffer_peak, input_peak * 0.85)` — a
decaying peak of the mic input the UI shows as a level dot (and which surfaces a
denied-microphone-permission situation: the dot never lights up).

## Export

`AudioEngine::export_session(dir)` writes every sample as a float32 WAV
(`sample-<id>-bank<N>.wav`, via `engine/src/wav_writer.cpp`) plus a `session.json`
containing bar length, beats/bpm, extra latency, and per-sample metadata (bank, offset,
length, volume, speed, reverse, repeat, start offset, master flag). Returns the number
of files written.

PRECONDITION: audio I/O must be stopped — the function walks RT-thread-owned sample
data from the calling thread. `bridge.cpp::nativeExport` handles this: it stops the
miniaudio device, exports, then restarts it (a sub-second audible gap). The UI's SAVE
button exports to `~/Music/TapLooper/<timestamp>/`.

## Command set

All 23 commands are defined in `engine/src/commands.h`. Integer ordinal = the value
transmitted over JNI (see `shared/…/bridge/CommandType.kt` for the Kotlin constants).

| Ordinal | Name | Parameters used |
|---------|------|-----------------|
| 0 | `StartRecording` | `bank`, `fx`, `fy` |
| 1 | `StopRecording` | — |
| 2 | `TapSample` | `sample_id` |
| 3 | `DeleteSample` | `sample_id` |
| 4 | `SetSampleVolume` | `sample_id`, `fvalue` (0..1) |
| 5 | `SetSampleReverse` | `sample_id`, `bvalue` |
| 6 | `SetSampleSpeed` | `sample_id`, `ivalue` (0=Half, 1=Normal, 2=Double) |
| 7 | `MoveSample` | `sample_id`, `bank`, `fx`, `fy` |
| 8 | `SetBankMute` | `bank`, `bvalue` |
| 9 | `SetBankVolume` | `bank`, `fvalue` |
| 10 | `SetBankMode` | `bank`, `ivalue` (0=Loop, 1=OneShot) |
| 11 | `FireAllInBank` | `bank` |
| 12 | `CaptureLastLoop` | `bank` |
| 13 | `SetBeatsPerBar` | `ivalue` (2, 4, or 8) |
| 14 | `Reset` | — |
| 15 | `SetMasterPlaying` | `bvalue` |
| 16 | `SetBankSolo` | `bank`, `bvalue` |
| 17 | `SetSampleStartOffset` | `sample_id`, `ivalue` (frames, signed) |
| 18 | `SetMasterBarFrames` | `ivalue` (frames; clamped 100 ms – 60 s) |
| 19 | `SetSampleRepeat` | `sample_id`, `bvalue` |
| 20 | `SetExtraLatencyFrames` | `ivalue` (frames, signed) |
| 21 | `CancelRecording` | — |
| 22 | `SetMetronome` | `bvalue` |

## Threading invariants

The RT audio thread (miniaudio callback) must never:
- Allocate or free heap memory
- Lock a mutex or use any blocking synchronisation
- Make system calls

The `CommandQueue<256>` is a single-producer / single-consumer lock-free ring buffer
(power-of-two, atomic head/tail). The `StatePublisher` uses a mutex only briefly to
swap a `shared_ptr` — the RT thread holds it for nanoseconds. State is published
immediately when commands were applied or a recording is active, otherwise every
`kPublishEveryN` (8) callbacks — the UI polls at ~33 ms, so per-callback publishing
would be wasted copies. The mixer input list is a member vector reused across
callbacks (no steady-state allocation).

**Known MVP exceptions** (acceptable; infrequent one-off events):
- `StopRecording` shrinks the sample's PCM vector to its recorded length and, for the
  first recording, reallocates the rolling buffer to match `master_bar_frames`.
- `CaptureLastLoop` allocates a new `Sample` and a temporary `std::vector<float>`.
- `Reset` calls `samples.clear()` which destructs `unique_ptr`s (heap frees on RT thread).
- `publish_state` does one `make_shared<EngineState>` copy per publish (see cadence above).
