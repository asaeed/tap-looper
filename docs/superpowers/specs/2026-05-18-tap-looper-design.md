# Tap Looper — Design Spec

**Date:** 2026-05-18
**Project root:** `~/claude/tap-looper`

A live looping app for Android (with a shared C++ audio core that can be ported to iOS later). Phone-as-interface for an advanced sampling looper: real-time recording and playback of loops and one-shot samples, organized in banks, with first-loop-defines-grid quantization and BPM-driven visual pulse.

---

## Goals

- **Real-time latency**: round-trip ≤ 20 ms on a modern Android device; no audible gap between loop iterations.
- **Hands-free recording**: any tap on the screen stops an in-progress recording (so the user can grab the phone, hit the screen with any finger, and keep playing their instrument).
- **Up to 8 banks**, each containing many samples, with bank-level mode (loop or one-shot) and live mixing.
- **Cross-platform path**: Android UI ships first; a shared C++ engine makes future iOS port realistic.
- **Themeable visuals**: v1 ships with one default theme ("Neon Arcade"); additional themes added later.

## Non-goals (v1)

These are explicit deferrals — engineering does not need to plan for them in v1:

- Persistence between sessions (sessions wipe on app close)
- Audio export / bouncing
- Metronome / count-in
- Undo
- USB device selection UI (rely on Android's default audio routing)
- True audio-onset BPM detection (BPM is derived from loop length in v1)
- iOS UI shell (engine will be portable; iOS shell is a future project)
- Real-time effects beyond reverse and speed change (no reverb, no filter, etc.)
- Per-sample EQ
- Network sync, MIDI clock sync, external sync

---

## Architecture

```
┌──────────────────────────────────────────┐
│        KOTLIN UI  (Jetpack Compose)      │
│  Banks, sample boxes, modal, animations  │
└──────────────────┬───────────────────────┘
                   │  thin Kotlin wrapper
                   │  exposes engine commands
                   │  + observable state flows
┌──────────────────▼───────────────────────┐
│            JNI bridge (slim)             │
│   ~1 KB of glue; commands in, state out  │
└──────────────────┬───────────────────────┘
                   │
┌──────────────────▼───────────────────────┐
│     C++17 AUDIO ENGINE  (portable)       │
│  ┌──────────────────────────────────┐    │
│  │ Sample store / mixer / scheduler │    │
│  │ Rolling capture buffer           │    │
│  │ BPM derivation                   │    │
│  └──────────────┬───────────────────┘    │
│                 │                        │
│  ┌──────────────▼───────────────────┐    │
│  │   Oboe (Google low-latency I/O)  │    │
│  │   AAudio → ALSA on Android       │    │
│  └──────────────────────────────────┘    │
└──────────────────────────────────────────┘
```

### Key principles

- **No audio data crosses the JNI boundary.** Only control messages (commands) flow in; state events flow out. All PCM stays in C++ memory. Copying audio across JNI would add latency and break the real-time guarantee.
- **The audio engine is portable.** It depends only on Oboe (which it abstracts behind an `AudioIO` interface). The iOS port replaces the Oboe implementation with one using `AVAudioEngine` or `AURemoteIO`.
- **Three-thread model**: real-time audio thread (Oboe callback), worker thread (non-RT operations like rolling-buffer copy-out), UI thread (commands and state observation). No locks on the RT thread.

### Tech stack

| Layer | Choice | Notes |
|---|---|---|
| Android UI | Kotlin + Jetpack Compose | Declarative, animation-friendly, native Canvas for sample boxes |
| Audio I/O | Oboe (Android) / AVAudioEngine (iOS, later) | Round-trip latency target: ≤ 20 ms on a Pixel-class device |
| Audio engine | C++17, NDK + CMake | Portable; runs on host for unit tests |
| Bridge | JNI (slim) | Kotlin → engine: commands; engine → Kotlin: state events via Flow |
| Animations | Compose animations | Pulse driven by BPM-derived rate |
| Persistence | None (v1) | In-memory only |
| Testing | gtest (engine, host-runnable), Compose UI tests | Engine validatable without a device |

### Project layout

```
~/claude/tap-looper/
├── app/                               # Android app (Kotlin + Compose)
│   ├── src/main/kotlin/com/taplooper/
│   │   ├── ui/                        # Compose screens & components
│   │   ├── bridge/                    # Kotlin wrapper around the C++ engine
│   │   ├── domain/                    # UI-side models, state holders
│   │   └── MainActivity.kt
│   ├── src/main/cpp/
│   │   └── bridge.cpp                 # JNI implementations (Java↔C++ glue)
│   └── build.gradle.kts
├── engine/                            # Portable C++ audio engine
│   ├── src/
│   │   ├── audio_engine.{h,cpp}
│   │   ├── sample.{h,cpp}
│   │   ├── mixer.{h,cpp}
│   │   ├── rolling_buffer.{h,cpp}
│   │   ├── bpm.{h,cpp}
│   │   ├── command_queue.{h,cpp}      # lock-free SPSC queue
│   │   ├── state_snapshot.{h,cpp}     # atomic state for UI to read
│   │   └── platform/
│   │       ├── audio_io.h             # platform-agnostic interface
│   │       ├── audio_io_oboe.cpp      # Android implementation
│   │       └── audio_io_host.cpp      # host build (offline file I/O for tests)
│   ├── test/                          # gtest unit tests (host)
│   └── CMakeLists.txt
├── docs/superpowers/specs/            # design + planning artifacts
├── build.gradle.kts
└── settings.gradle.kts
```

---

## Audio engine

### Oboe configuration

- **Sample rate**: 48 kHz (the Android low-latency path target).
- **Buffer size**: smallest the device permits — typically 96–192 frames (~2–4 ms one-way).
- **Performance mode**: `LOW_LATENCY` with exclusive access where supported.
- **Streams**: one input (system default device — mic, or USB interface if Android routes there) plus one stereo output. The engine callback processes both per cycle.

### Threading

- **RT audio thread**: owned by the Oboe callback. Mixes output, captures input, services rolling buffer. **No locks, no allocations, no syscalls**, no logging from this thread. State changes from the UI arrive via a lock-free single-producer / single-consumer queue.
- **Worker thread**: handles non-RT operations — rolling-buffer copy-out on "capture last," any future file I/O. Communicates with the RT thread via the same queue pattern but in the opposite direction.
- **UI thread**: sends commands via the lock-free queue; reads engine state from atomic snapshots that the engine publishes after each callback. Never touches PCM memory directly.

### Sample data model

The sample model is split into **immutable** fields (set at recording time, never modified afterward — the foundation of the non-destructive guarantee) and **mutable** fields (state and user-controlled properties).

**Immutable after recording:**

| Field | Type | Notes |
|---|---|---|
| `id` | uint64 | Unique within a session |
| `pcm` | float32[] (stereo interleaved or mono) | The actual audio data. **Never modified** after recording stops — not by mode flips, not by reverse, not by speed. |
| `recorded_length_frames` | size_t | Recorded duration in frames (equals `pcm.size / channels`) |
| `recorded_offset_in_bar` | frames | Bar-relative offset of recording start. **Captured for every sample regardless of bank mode** (0 if `L` was not yet set when this sample was recorded). |

**Mutable:**

| Field | Type | Notes |
|---|---|---|
| `bank` | uint8 (0–7) | Which bank the sample belongs to (changes via drag). The sample's effective mode = `banks[bank].mode` — there is no per-sample mode override. |
| `volume` | float (0.0–1.0) | Per-sample volume |
| `reverse` | bool | If true, mixer reads PCM backward |
| `speed` | enum {HALF, NORMAL, DOUBLE} | Playback rate; engine resamples |
| `enabled` | bool | LOOP mode: whether this sample's loop is enabled. ONE-SHOT: not used. |
| `recording` | bool | True while the sample is being recorded (writable buffer) |
| `position_in_bank` | (x, y) float | Visual position within its bank (changes via drag) |

**Derived on demand (not stored):**

For a sample whose bank is in LOOP mode:
- `offset_in_bar = recorded_offset_in_bar`
- `loop_length = nearest_multiple_of_L(recorded_length_frames)` — at least `L`
- `bar_multiple = loop_length / L`

For a sample whose bank is in ONE-SHOT mode:
- `play_length = recorded_length_frames` (raw, no quantization)

**Non-destructive bank/mode change guarantee:** A sample's playback behavior is fully determined by its `bank` field plus the bank's current mode. PCM and recording metadata are immutable. Dragging a sample to a different bank, or toggling a bank's mode, only changes which interpretation the mixer applies. Reverting the change returns the sample to identical behavior.

### Recording — first sample

1. User taps an empty area in a bank → UI sends `START_RECORDING(bank, position)` command.
2. Engine state goes to `RECORDING`. It captures input PCM into a growing buffer.
3. User taps anywhere → UI sends `STOP_RECORDING`.
4. Engine computes `L = capture_end − capture_start` and sets it as the **global master bar length**.
5. The new sample's `offset_in_bar = 0` and `bar_multiple = 1`.
6. The engine establishes a global play head (cycling 0 → L) and starts the sample looping from offset 0 (which is "now").

### Recording — subsequent samples

1. UI sends `START_RECORDING(bank, position)`. Recording starts **immediately** on tap (no waiting for next downbeat — responsiveness wins over perfect-start sync).
2. Engine records actual frames captured. **For every recording (regardless of bank mode)**, the engine also captures `recorded_offset_in_bar` — the bar-relative offset where recording started, or 0 if `L` is not yet set. Storing this universally lets a ONE-SHOT sample preserve its original timing if it's later dragged into a LOOP bank.
3. UI sends `STOP_RECORDING`. Engine computes raw duration `d`.
4. Universal minimum: if `d < 200 ms`, discard the recording. UI shows a brief "too short" flash on the box and removes it.
5. Otherwise, the sample is stored with its **immutable fields** (`pcm`, `recorded_length_frames`, `recorded_offset_in_bar`) and assigned to the bank. PCM is stored at actual length — no padding, no truncation, no quantization. The bank's current mode determines how the mixer interprets the data:
   - **Bank in LOOP mode, `recorded_length_frames ≤ L`** (sub-bar): mixer schedules the sample to play at `recorded_offset_in_bar` every bar. The PCM plays for its actual duration; silence fills the rest of each iteration.
   - **Bank in LOOP mode, `recorded_length_frames > L`** (multi-bar): derived `loop_length = nearest_multiple_of_L(recorded_length_frames)` (minimum `2L`). Mixer plays the PCM continuously, looping every `loop_length` frames. The sample plays for `recorded_length_frames` and the remainder (if `loop_length > recorded_length_frames`) is silent.
   - **Bank in ONE-SHOT mode**: mixer plays the sample once from start when manually triggered (tap on box, or bank "▶ ALL"). No quantization, no scheduling.

Crucially: the recorded PCM is never rewritten. Toggling a bank's mode, or dragging a sample into a different-mode bank, only changes which interpretation the mixer applies.

### Sub-bar sample tail overlap

If a sub-bar LOOP sample has `offset_in_bar + length_frames > L`, its audio tail spills into the next bar. The mixer allows this — the next iteration starts at the same `offset_in_bar` of the next bar, briefly co-playing with the previous iteration's tail. This is musically benign and avoids artificially truncating the sample.

### Mixer

The audio callback runs every ~4 ms and processes one buffer of `N` frames:

1. For each sample with `playing == true` (LOOP: enabled and within its scheduled play window; ONE-SHOT: currently triggered and not yet finished):
   - Determine the read position in the sample's PCM, based on `speed` and `reverse`.
   - Read `N` frames (resampling if `speed ≠ NORMAL`).
   - Apply `sample.volume`.
   - Mix into the output bus, scaled by `bank.volume × (bank.muted ? 0 : 1)`.
2. The global play head advances by `N` frames, modulo `L × max_bar_multiple` (or just `L` if all samples are sub-bar).
3. For LOOP samples, the mixer checks at each bar boundary whether the sample should start a new iteration (i.e., if the current play position matches `offset_in_bar` of any sample).
4. Output buffer is written to Oboe; input buffer is read from Oboe and pushed to the rolling buffer + any active recording.

### Rolling buffer ("Capture Last Loop")

- Engine maintains a circular buffer of input audio, length = `L` (or 8 s fallback if `L` is not yet set).
- "Capture last" command snapshots the buffer's contents and creates a new sample in the focused bank, applying the same mode-dependent storage rules as a normal recording.
- The copy-out happens on the worker thread (allocating a new sample buffer is not RT-safe).

### BPM derivation

- `bpm = (beats_per_bar × 60) / L_seconds`, with `beats_per_bar` defaulting to 4.
- UI exposes `÷2` and `×2` buttons that cycle `beats_per_bar` through `{2, 4, 8}`.
- `bpm` is published as part of the engine's atomic state snapshot. The UI uses it to drive the visual pulse rate (`pulse_hz = bpm / 60`) on playing samples.
- BPM does not affect audio behavior. True audio-onset BPM detection is out of scope for v1.

---

## UI / interaction

### Screen layout (portrait-locked)

```
┌─────────────────────────────────────┐
│  ● 4.2s    ⎘ CAPTURE        ●  ○   │ ← top bar
├─────────────────┬───────────────────┤
│  BANK 1   LOOP  │  BANK 2   LOOP    │ ← bank header: label + mode toggle
│       MUTE      │       MUTE        │ ← context action (LOOP→MUTE, ONE→▶ALL)
│                 │                   │
│   ┌─┐    ┌─┐    │   ┌─┐  ┌─┐        │ ← sample boxes at tap positions
│   └─┘    └─┘    │   └─┘  └─┘        │
│                 │                   │
│  vol ▮▮▮▮▮▯▯    │  vol ▮▮▮▮▮▮▮     │ ← bank volume (vertical drag)
├─────────────────┼───────────────────┤
│  BANK 3   ONE   │  BANK 4   LOOP    │
│      ▶ ALL      │      MUTE         │
│                 │                   │
│ ┌─┐ ┌─┐ ┌─┐     │  ┌──────┐         │
│ └─┘ └─┘ └─┘     │  │ REC  │         │ ← pulsing red box = active recording
│                 │  └──────┘         │
│  vol ▮▮▮▮▮▮▮    │  vol ▮▮▮▯▯▯▯     │
├─────────────────┴───────────────────┤
│            BPM 92    ÷2  ×2         │ ← BPM display + correction buttons
└─────────────────────────────────────┘
```

### Banks

- **8 total**, split across **2 pages of 4** banks each.
- **2 × 2 grid** layout per page (each bank occupies one quadrant of the body area).
- Each bank has a fixed accent color (8 distinct neon hues; banks 1–4 on page 1, 5–8 on page 2).
- **Page switching**: tap a page pip in the top bar, or swipe horizontally. **Disabled while a recording is in progress** (avoids accidental page change during the "tap anywhere to stop" rule).

### Bank header controls

Each bank's top strip contains:

- **Bank label** (e.g., "BANK 1"), color-coded.
- **LOOP / ONE mode toggle**: tap to flip the bank's mode. All samples in the bank instantly play under the new mode (mode is a bank property; samples inherit it).
- **Context action button**:
  - In LOOP mode → reads **"MUTE"**. Tap toggles bank master mute.
  - In ONE-SHOT mode → reads **"▶ ALL"**. Tap fires every sample in the bank simultaneously (each plays once from start).

### Bank body

- Tap on empty area → start recording. New sample box appears at the tap position with a recording animation.
- Tap on an existing sample box → behavior depends on the sample's mode (which inherits from the bank by default).
- Bank volume is set by touching or dragging horizontally along the bottom-edge strip (a thin strip ~12 px tall). Tap position from left to right = level from 0 to max; drag updates continuously.

### Samples

- Sample box position within the bank reflects the user's original tap location (visual organization only; positions have no audio meaning).
- Sample box dimensions are fixed for v1 (uniform across all samples). Optional length-based size variation is deferred to v1.1 polish.
- Samples render in z-order of creation; most recent on top. Overlap is allowed.
- Soft cap: 8 samples per bank (configurable; can be lifted in v1.1 if needed). Beyond the cap, an attempt to record shows a "bank full" message.

### Gesture map

| Gesture | Target | Action |
|---|---|---|
| Tap | Empty bank body | Start recording → new box at tap position |
| Tap | Anywhere on screen, while recording | Stop recording |
| Tap | LOOP-mode sample box | Toggle `enabled` (sample stops/starts; phase stays locked to bar grid when re-enabled) |
| Tap | ONE-SHOT-mode sample box | Trigger from start (retriggers from beginning if tapped again) |
| Tap | "MUTE" button (bank header, LOOP) | Toggle bank master mute |
| Tap | "▶ ALL" button (bank header, ONE-SHOT) | Fire all samples in bank simultaneously |
| Tap | LOOP / ONE mode toggle | Flip bank mode (all samples in bank instantly play under the new mode) |
| Long-press (~500 ms) | Sample box | Open sample modal |
| Drag | Sample box | Reposition (intra-bank or inter-bank). Cross-mode drag flips `mode` non-destructively. Disabled during recording. |
| Touch or horizontal drag | Bank's bottom-edge volume strip | Set bank volume — touch position (left→right) = level (0→max); drag updates continuously |
| Tap | "Capture Last" button (top bar) | Snapshot rolling buffer → new sample in focused bank |
| Tap | Page pip (top bar) | Switch page (1–4 ↔ 5–8). Disabled during recording. |
| Horizontal swipe | Anywhere | Switch page. Disabled during recording. |
| Tap | BPM ÷2 / ×2 buttons (bottom strip) | Change `beats_per_bar` (cycles 2 / 4 / 8) |

### Sample modal

Triggered by long-press on a sample box. A bottom sheet slides up — partial coverage so the rest of the UI remains visible:

```
┌──────────────────────────────────┐
│  ● Bank 2 / Sample 3              │
│                                   │
│  Volume     ▮▮▮▮▮▮▮▯▯▯   85       │
│  Reverse    [ off ]   [ on ]      │
│  Speed      [ ½ ]  [ 1 ]  [ 2 ]   │
│                                   │
│            [   DELETE   ]         │
└──────────────────────────────────┘
```

- Volume: horizontal slider, 0–100.
- Reverse: toggle.
- Speed: three buttons (½×, 1×, 2×). Engine resamples on playback.
- DELETE: prominent button at the bottom. No additional confirmation needed because the long-press → modal → tap-DELETE sequence is already three intentional acts.

The modal does **not** include a mode control. Mode is a property of the bank, not the sample — to change a sample's mode, drag it into a different-mode bank or toggle its current bank's mode.

Dismiss by tapping outside the sheet or swiping down.

### Focused bank

- The "focused bank" is the bank whose mode toggle, volume strip, mute/fire-all button, or sample box was last touched.
- Visual: a subtle 1 px outline highlight on the focused bank's perimeter.
- Used by the "Capture Last Loop" button to know which bank to drop the captured sample into.

### Recording visual

- During recording, the sample box appears at the tap location with a pulsing red-orange border (faster pulse than normal playback).
- A duration counter in the top bar shows the in-progress recording length.
- On stop:
  - If duration < 200 ms: the box fades out with a brief "too short" flash (~300 ms).
  - Otherwise: the box transitions to its idle / playing state per its bank's mode.

### Drag to reposition (and cross-bank / cross-mode move)

A sample box can be dragged to a new visual position. The drag may cross into another bank — if that bank has a different mode, the sample's mode is flipped non-destructively (see "Sample data model" above).

**Gesture resolution.** Touch-down on a sample box enters a brief "ambiguous" window:

- If the finger moves more than ~12 px within 150 ms → resolves as **drag**.
- If the finger is released within ~150 ms with little movement → resolves as **tap**.
- If the finger stays in place for ~500 ms without crossing the move threshold → resolves as **long-press** (opens the modal).

These thresholds tune later; initial values listed are starting points.

**Drag behavior.**

- Drag is **disabled while a recording is in progress.** A touch during recording always resolves to "stop recording" — recording stop takes priority over any other gesture.
- During drag, the sample box follows the finger and renders slightly elevated (e.g., a subtle shadow or +10% scale).
- The bank under the finger becomes a **drop target** with a soft outline pulse in the target bank's color.
- If the drop target bank has a different mode than the sample's current mode, the target's pulse animation includes a small mode-conversion glyph (`LOOP ↔ ONE`) so the user knows what will happen on release.
- On release:
  - Drop point is inside the original bank → only `position_in_bank` updates.
  - Drop point is inside a different bank → `bank` updates; `position_in_bank` is set to the local drop coordinates inside the new bank; the sample takes on the new bank's color.
  - If the new bank has a different mode → the sample plays under the new bank's mode. **PCM and `recorded_offset_in_bar` are untouched** — the mixer just reinterprets the same data.
- Drag is confined to the currently-visible page. To move a sample to a bank on the other page: drop somewhere on the current page first, swipe to the other page, then drop again. Cross-page drag-and-hold is not v1.

**First-loop edge case.** If `L` has not yet been set (no LOOP-mode samples exist), and a sample originally recorded into a ONE-SHOT bank is dragged into a LOOP bank, that sample becomes the first LOOP sample. Its `recorded_length_frames` sets `L`; its `recorded_offset_in_bar` was captured at 0 (since `L` wasn't set at record time), so it plays from bar 0.

**Conversion reversibility.** Because PCM and `recorded_offset_in_bar` are immutable, dragging a sample LOOP → ONE-SHOT → LOOP returns it to bit-exactly its original behavior. The user can experiment freely.

---

## Visual style — Neon Arcade (v1 default)

- **Background**: pure black (`#000`).
- **Bank accent colors** (page 1 / page 2):
  - Bank 1 / 5: cyan `#00ffd5`
  - Bank 2 / 6: magenta `#ff00aa`
  - Bank 3 / 7: violet `#9d00ff`
  - Bank 4 / 8: red-orange `#ff4d00`
- **Sample box at rest**: 1.5 px solid border in bank color, semi-transparent fill (~15% opacity).
- **Sample box playing**: solid fill in bank color; **gentle** glow (max ~12 px spread); pulse at `bpm / 60` Hz with a 1.04× scale animation.
- **Sample box recording**: red-orange pulsing border, slightly faster pulse (~1.5× the BPM-derived rate).
- **Sample box disabled / muted**: dashed border, 40% opacity, no fill.
- **Typography**: monospace font (JetBrains Mono or similar) for all labels — BPM, bank labels, modal headers.
- **Animations**: 200 ms ease-out for state transitions; infinite pulse on playing samples; smooth fade for recording-too-short flash.
- **Themeability**: visual rules live in a `Theme` object; the rest of the UI reads colors and animation parameters from it. Additional themes (e.g., Cyberpunk Glitch, Liquid Iridescent from the brainstorm) can be added in later releases by adding new `Theme` instances.

---

## Cross-platform path (iOS, post-v1)

When ready to ship iOS:

- The `engine/` subproject compiles unchanged on iOS via Xcode + the same CMakeLists.
- Implement `engine/src/platform/audio_io_avaudio.cpp` (or `_remoteio.cpp`) to satisfy the same `AudioIO` interface as the Android Oboe implementation.
- Replace the Compose UI with a SwiftUI shell that calls into the engine via a Swift wrapper (similar pattern to the Kotlin bridge).
- Estimated effort: ~30 % of original project (engine, audio scheduling, mixer logic, BPM, rolling buffer all stay; UI and platform glue are rewritten).

---

## Testing

- **Engine unit tests** (gtest, runnable on the host with no device):
  - Feed PCM into the recording flow; assert correct sample length, `offset_in_bar`, and bar quantization.
  - Verify mixer output by feeding known samples and asserting bit-exact mixed buffers (with floating-point tolerance).
  - Rolling buffer correctness (capture-last produces the previous L seconds).
  - BPM derivation correctness across `beats_per_bar` values.
- **Compose UI tests**:
  - Each gesture invokes the correct engine command (engine mocked at the bridge layer).
  - Visual states reflect engine state snapshots.
- **Manual on-device tests**:
  - **Latency check**: with a physical audio loopback (output cable → input jack), record a known impulse pattern and measure round-trip delay.
  - **Drift check**: record 10 s of metronome, listen to the loop for 5 minutes — no audible drift between iterations.
  - **Bank-full handling**: record beyond the soft cap, verify behavior.

---

## Open questions and known gaps

Items intentionally deferred to the implementation plan or v1.1 — not blocking for the spec:

- **Long-press duration**: target ~500 ms; final value validated during user testing.
- **Sample box layout when bank gets crowded**: position is free-form (tap location). Overlap allowed. Soft cap = 8 per bank; on cap reached, attempting to record shows "bank full." Per-sample cap can be lifted in v1.1.
- **Audio stream interruption**: if Oboe stream resets mid-recording (e.g., USB device unplugged), discard the in-progress recording and surface an error toast.
- **First-launch tooltip**: a one-time hint noting that sessions don't persist (since persistence is out of scope for v1).
- **Volume curve**: bank volume and per-sample volume sliders need a perceptual mapping (probably log/cube-root) rather than linear gain. To be decided during implementation.

---

## Glossary

- **Bank**: A group of samples sharing a default mode (LOOP or ONE-SHOT), volume, and visual color. 8 total, organized in two pages of 4.
- **Sample**: A recorded audio clip belonging to a bank. Has its own volume, mode, reverse, speed, and (for LOOP-mode samples) bar offset.
- **L (master bar length)**: Duration of the first recorded loop, in seconds. All subsequent LOOP-mode samples are quantized to whole multiples of L. Set once per session; does not change after the first recording.
- **offset_in_bar**: For LOOP samples shorter than L, the bar-relative time at which the user started recording. Playback fires at this same offset every bar.
- **Focused bank**: The bank most recently touched (any control). Receives samples from the "Capture Last Loop" button.
- **Rolling buffer**: Continuously-overwritten circular buffer of input audio, length = L. Source for the "Capture Last Loop" feature.
- **RT thread**: The real-time audio thread, owned by Oboe's callback. Lock-free, allocation-free, syscall-free.
