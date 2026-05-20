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
| `recorded_length_frames` | `Frames` (`uint32_t`) | PCM length after trim; never changes post-record |
| `recorded_offset_in_bar` | `Frames` | Bar-relative playback position; set at StartRecording with latency compensation |
| `pcm` | `std::vector<float>` | Interleaved stereo samples |
| `is_master_loop` | `bool` | True for the first recorded loop; affects play-head init and LAT-shift exclusion |

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

Allocates a new `Sample` with capacity `kMaxRecFrames` (60 s at 48 kHz = 2,880,000 frames).
Position x/y are taken from the `Command.fx/fy` fields (normalised bank coordinates).

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

The stop sequence applies four post-processing steps in order:

#### 1. Leading-silence trim + loudest-onset detection

Constants:
- `kSilenceThreshold = 0.02f` (~-34 dB) — minimum amplitude to be considered "audible"
- `kOnsetThreshold = 0.05f` (~-26 dB) — minimum amplitude to be considered an onset
- `kMinSilenceBetween = 480` frames (10 ms at 48 kHz) — gap required between distinct onsets
- `kClickScanFrames = 24000` frames (500 ms) — scan window for click detection

Step A scans forward from frame 0 for the first frame above `kSilenceThreshold` —
this is `first_audible`.

Step B then scans the first 500 ms for all distinct onsets above `kOnsetThreshold`,
tracking the loudest peak found. A trackpad or mouse click registers as a soft transient
(peak ~0.05–0.20) while a real drum hit registers much louder (peak ~0.4–1.0). If the
loudest onset is both later than `first_audible` and has peak > 0.10, the engine advances
`first_audible` to that onset.

The PCM is then shifted backward by `first_audible` frames (`memmove`), and
`recorded_offset_in_bar` is advanced by the same amount so playback timing stays correct.

#### 2. Trailing-onset trim

The engine scans the full (already leading-trimmed) recording for onsets with the same
`kOnsetThreshold`/`kMinSilenceBetweenOnsets` parameters. It counts all onsets and records
the position of the last one (`last_onset_pos`).

Trim fires only if:
- `onset_count >= 2` (at least two rhythmic events exist — avoids cutting off single-hit samples)
- `last_onset_pos > recorded_length_frames / 2` (the last onset is in the latter half)

When trim fires, `recorded_length_frames` is set to `last_onset_pos`. The intent: the final
onset (typically the button/key the user pressed to stop) is the musical downbeat for the
next iteration, so the loop ends just before it — the wrap from end to start IS the downbeat,
preventing a doubled hit at the loop boundary.

#### 3. Edge fades

```cpp
constexpr Frames kFadeBase = 96;  // 2 ms at 48 kHz
Frames fade_len = std::min(kFadeBase, rec.recorded_length_frames / 4);
```

A linear ramp-up is applied to the first `fade_len` frames and a ramp-down to the last
`fade_len` frames. This eliminates click/pop at playback start and at the loop-wrap
boundary. Capped to 25% of the recording so short samples are not dominated by the fades.

#### 4. Master bar / play-head init

If this is the first recording (`master_bar_frames == 0`), the engine:
- Marks the sample as `is_master_loop = true`
- Sets `master_bar_frames = recorded_length_frames` (after trims)
- Resizes the rolling buffer to match `L`
- Fast-forwards `global_play_head` by `round_trip_latency_frames + extra_latency_frames`
  modulo `L` (see Latency Compensation below)

If the recording is too short (< `kMinRecordingMs` = 200 ms), it is silently discarded.

## Latency compensation

### Auto-detected round-trip latency

After the miniaudio device initialises, `MiniaudioIO::start` computes:

```cpp
int input_frames  = device_.capture.internalPeriodSizeInFrames
                  * device_.capture.internalPeriods;
int output_frames = device_.playback.internalPeriodSizeInFrames
                  * device_.playback.internalPeriods;
int round_trip = input_frames + output_frames;
engine.set_round_trip_latency_frames(round_trip);
```

Typical values on a Mac with `periodSizeInFrames=128, periods=2`:
input = 256 frames, output = 256 frames, round_trip = 512 frames (~10.7 ms at 48 kHz).
The actual measured value is printed to stderr on startup.

### User-tunable extra latency

`extra_latency_frames` (default 2400 = 50 ms at 48 kHz) accounts for the residual
hardware/speaker/air latency that miniaudio cannot measure. The user adjusts this live
with the ±2 ms buttons in BottomBar. The value is signed; negative values advance
recordings earlier in the bar.

### Where each component is applied

**At StartRecording** (non-master recordings):
```
recorded_offset_in_bar = (play_head + L - round_trip_latency % L) % L
```
This shifts the recording's grid position backward by the round-trip latency so playback
lands in phase with what the user heard.

**At mix time** (non-master samples only, in `mixer.cpp`):
```cpp
int64_t shifted = int64_t(offset) - int64_t(extra_latency_frames);
offset = Frames(((shifted % L) + L) % L);
```
Applying `extra_latency` at mix time — rather than baking it into `recorded_offset_in_bar`
at record time — means the user can dial the LAT knob live and all existing recordings
shift together without re-recording.

**At master StopRecording** (first loop only):
```cpp
int shift = round_trip_latency_frames + extra_latency_frames;
global_play_head = Frames(shift % int(state.master_bar_frames));
```
Fast-forwarding the play head means the first audible downbeat of the master loop is heard
one full bar after the stop gesture, aligned with the user's musical expectation, rather
than `round_trip` ms after the click.

## Mixer (`engine/src/mixer.cpp`)

`mix(samples, banks, bar_frames, play_head, frame_count, out, extra_latency_frames)`
is called once per audio callback after commands are applied. The output buffer is
pre-zeroed by the caller.

### Solo / mute logic

If any bank has `soloed = true`, only soloed banks contribute to output (all others are
silenced regardless of their mute flag). If no bank is soloed, `muted` banks are skipped.

### ONE-SHOT samples

When `bank.mode == OneShot` and `s.playing == true`, the sample plays from
`one_shot_play_pos` advancing by `speed_ratio(s.speed)` per output frame via
`read_sample_lerp`. When `one_shot_play_pos >= recorded_length_frames` the mixer sets
`s.playing = false` (in-place mutation; the RT thread is the sole writer).

### LOOP samples — three branches

After computing `offset` (with the LAT shift for non-master samples), the mixer chooses
a branch based on `s.repeat` and `sample_len` vs `bar_frames`:

#### Branch 1: Repeat

```cpp
float effective_play_len = sample_len / speed_ratio(s.speed);
Frames tile_len = min(effective_play_len, bar_frames);
```

The sample tiles continuously across the bar starting from `offset`. Speed-aware:
at 2× speed `effective_play_len` is halved, so the sample tiles twice per bar.
At ½ speed it may overflow the bar — in that case `tile_len` is clamped to `bar_frames`.

`start_offset_frames` rotates within `tile_len` using modular arithmetic so the
rotation wraps cleanly.

#### Branch 2: Sub-bar non-repeat (`sample_len <= bar_frames`)

The sample plays once per bar in the window `[offset, offset + sample_len)` with wrap
handling when the window straddles the bar boundary. `start_offset_frames` rotates
within `recorded_length_frames`.

#### Branch 3: Multi-bar non-repeat (`sample_len > bar_frames`)

`loop_len = nearest_multiple_of_L(sample_len, bar_frames)` (minimum 2L). The
`global_play_head` is used modulo `loop_len`; frames past `sample_len` (trailing padding
within the iteration) are silent.

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

## Command set

All 21 commands are defined in `engine/src/commands.h`. Integer ordinal = the value
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

## Threading invariants

The RT audio thread (miniaudio callback) must never:
- Allocate or free heap memory
- Lock a mutex or use any blocking synchronisation
- Make system calls

The `CommandQueue<256>` is a single-producer / single-consumer lock-free ring buffer
(power-of-two, atomic head/tail). The `StatePublisher` uses a mutex only briefly at the
end of `process()` to swap a `shared_ptr` — the RT thread holds it for nanoseconds.

**Known MVP exceptions** (acceptable; infrequent one-off events):
- First `StopRecording` reallocates the rolling buffer to match `master_bar_frames`.
- `CaptureLastLoop` allocates a new `Sample` and a temporary `std::vector<float>`.
- `Reset` calls `samples.clear()` which destructs `unique_ptr`s (heap frees on RT thread).
