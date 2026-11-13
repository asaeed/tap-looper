# Architecture

## Layer diagram

```
┌─────────────────────────────────────────────────────┐
│  Compose UI  (shared/src/commonMain/kotlin/…/ui/)   │
│  TapLooperScreen / BankGrid / Bank / SampleBox …    │
└────────────────────┬────────────────────────────────┘
                     │ StateFlow<EngineStateKt>  (poll ~33ms)
                     │ vm.submit(command)
┌────────────────────▼────────────────────────────────┐
│  AppViewModel  (shared/…/domain/AppViewModel.kt)    │
│  EngineBridge  (shared/…/bridge/EngineBridge.kt)    │
└────────────────────┬────────────────────────────────┘
                     │ JNI  (System.loadLibrary("tap_looper_bridge"))
┌────────────────────▼────────────────────────────────┐
│  bridge.cpp  (desktop/src/jvmMain/cpp/bridge.cpp)   │
│  nativeSubmit / nativeSnapshot / nativeStart …      │
└─────────┬──────────────────────────┬────────────────┘
          │ push Command             │ read EngineState
┌─────────▼──────────┐   ┌───────────▼────────────────┐
│  CommandQueue<256> │   │  StatePublisher            │
│  (SPSC lock-free)  │   │  (mutex + shared_ptr swap) │
└─────────┬──────────┘   └───────────▲────────────────┘
          │ drain in process()        │ publish every callback
┌─────────▼──────────────────────────┴────────────────┐
│  AudioEngine  (engine/src/audio_engine.cpp)         │
│  apply(Command) → mix() → advance play_head         │
└────────────────────┬────────────────────────────────┘
                     │ process(in, out, frame_count)
┌────────────────────▼────────────────────────────────┐
│  AudioIO platform impl                              │
│  desktop: MiniaudioIO  (audio_io_miniaudio.cpp)     │
│  android: OboeIO       (planned, engine/platform/)  │
└────────────────────┬────────────────────────────────┘
                     │ duplex stream
           [microphone / speaker / audio hardware]
```

No PCM data crosses the JNI boundary. The bridge marshals only scalar state
(ints, floats, bools) from `EngineState` into flat `LongArray`/`FloatArray` buffers.

## Module responsibilities

### `engine/`
Portable C++17 library with zero platform dependencies (other than the `AudioIO` interface
it defines). Contains: `AudioEngine`, `Mixer`, `Sample`, `CommandQueue`, `RollingBuffer`,
`StatePublisher`, `BpmDerivation`. Builds as a static library (`tl_engine`) via
`engine/CMakeLists.txt`. The engine unit tests (`engine/test/`) link against this library
and run independently of Gradle.

### `shared/`
Kotlin Multiplatform module (`kotlin("multiplatform")`). Holds all Compose UI code in
`commonMain` so the same UI compiles for both JVM (desktop) and Android. Also contains
the JNI bridge (`EngineBridge.kt`), Kotlin-side state data classes (`EngineState.kt`),
and `AppViewModel`. No platform-specific code lives here today; future `androidMain` and
`desktopMain` source sets can be added for platform glue.

### `desktop/`
JVM module (`kotlin("jvm")`). Provides:
- `Main.kt` at `src/main/kotlin/com/taplooper/desktop/` — the Compose `application {}` entry point.
- `audio_io_miniaudio.cpp` — the `AudioIO` impl using miniaudio 0.11.21 for macOS/Linux duplex audio.
- `bridge.cpp` — all JNI entry points, builds as `libtap_looper_bridge.dylib`.
- `build.gradle.kts` — Gradle tasks that invoke CMake to build the native lib and copy it into
  `build/native-libs/` so `System.loadLibrary` finds it at runtime.

### `app/` (future)
Android shell. Not yet active (excluded from `settings.gradle.kts`). Will use the same
`shared` module for UI and will provide an `OboeIO` impl of `AudioIO`.

## Threading model

There are three active threads at runtime:

**RT audio thread** — driven by the miniaudio callback (`OnAudio`). Calls
`engine.process(in, out, frames)` once per audio buffer (~128 frames, ~2.7 ms). This
thread must never allocate heap, lock a mutex, or make system calls. Known MVP exceptions
(all infrequent one-off events): `StopRecording` shrinks the sample's PCM buffer and
reallocates the rolling buffer when `master_bar_frames` becomes known, and
`CaptureLastLoop` allocates a new `Sample`. Steady-state processing does not allocate:
the mixer input list is a reused member vector, and state publishing (which does one
`make_shared` copy) runs only when commands were applied, while recording, or every 8th
callback (`kPublishEveryN`) — the UI only polls every ~33 ms anyway.

**Kotlin/UI thread** — the Compose main thread. Reads state via `AppViewModel.state`
(a `StateFlow`), which is updated by the poll coroutine every ~33 ms. Sends commands
via `AppViewModel.*` helper functions, which call `EngineBridge.submit(...)`.

**Coroutine poll thread** (`Dispatchers.Default`) — `AppViewModel.init` launches a loop
that calls `bridge.snapshot()` every 33 ms and posts the result to `_state`. This is the
only thread that calls `nativeSnapshot` (i.e., locks the `StatePublisher` mutex).

### Command path (UI → RT thread)
1. User gesture → `AppViewModel.startRecording(bank, x, y)` (UI thread).
2. `bridge.submit(CT.StartRecording, bank=bank, fx=x, fy=y)` → `nativeSubmit` (JNI).
3. `bridge.cpp::nativeSubmit` constructs a `Command` struct and calls `engine->submit(c)`.
4. `engine->submit` calls `CommandQueue<256>::push(c)` — atomic store, no lock.
5. Next audio callback: `process()` drains the queue with `commands.pop(c)` and calls
   `apply(c)` — all on the RT thread.

### State path (RT thread → UI)
1. End of `process()` (immediately after commands / while recording, else every 8th
   callback): `publish_state()` increments `state.version`, mirrors the internal
   `samples` vector into `state.samples[]`, and calls `publisher.publish(state)`.
2. `StatePublisher::publish` takes a `std::mutex` lock, swaps in a new `shared_ptr<EngineState>`.
3. Poll coroutine (every 33 ms): `bridge.snapshot()` → `nativeSnapshot` → takes the same
   mutex, copies the `shared_ptr`, reads fields out into `LongArray`/`FloatArray`, releases.
4. `_state.value = bridge.snapshot()` triggers Compose recomposition.

The RT thread holds the mutex only for the duration of a `shared_ptr` swap (~nanoseconds).
The poll thread holds it only while reading fields. They never contend for long.

## JNI marshalling format

`nativeSnapshot` writes two flat arrays. Offsets are fixed; changing the layout requires
updating both `bridge.cpp` and `EngineBridge.kt::snapshot()`.

`kNumBanks = 8`, `kMaxSamples = 64`, `kEnvBins = 32`.

Array sizes: `LongArray(11 + 8*3 + 64*10)` = 675 longs;
`FloatArray(2 + 8 + 64*(4+32))` = 2314 floats.

**LongArray layout**

| Index | Field |
|-------|-------|
| 0 | `state.version` |
| 1 | `state.sample_count` |
| 2 | `state.focused_bank` |
| 3 | `state.master_bar_frames` |
| 4 | `state.recording` (0/1) |
| 5 | `state.recording_duration_frames` |
| 6 | `state.recording_sample_id` |
| 7 | `state.beats_per_bar` |
| 8 | `state.master_playing` (0/1) |
| 9 | `state.extra_latency_frames` |
| 10 | `state.metronome` (0/1) |
| 11 + i*3 | `banks[i].mode` (0=Loop, 1=OneShot), for i in 0..7 |
| 11 + i*3 + 1 | `banks[i].muted` (0/1) |
| 11 + i*3 + 2 | `banks[i].soloed` (0/1) |
| 11 + 8*3 + i*10 | `samples[i].id` |
| … + 1 | `samples[i].bank` |
| … + 2 | `samples[i].speed` (0=Half,1=Normal,2=Double) |
| … + 3 | `samples[i].enabled` (0/1) |
| … + 4 | `samples[i].playing` (0/1) |
| … + 5 | `samples[i].recording` (0/1) |
| … + 6 | `samples[i].recorded_length_frames` |
| … + 7 | `samples[i].recorded_offset_in_bar` |
| … + 8 | `samples[i].start_offset_frames` (signed) |
| … + 9 | `samples[i].repeat` (0/1) |

**FloatArray layout**

| Index | Field |
|-------|-------|
| 0 | `state.bpm` |
| 1 | `state.input_peak` (decaying mic peak, 0..1) |
| 2 + i | `banks[i].volume`, for i in 0..7 |
| 2 + 8 + i*36 | `samples[i].volume` |
| … + 1 | `samples[i].reverse` (0.0 or 1.0) |
| … + 2 | `samples[i].position_x` |
| … + 3 | `samples[i].position_y` |
| … + 4 .. + 35 | `samples[i].env[0..31]` (waveform envelope bins) |

Slots past `sample_count` are written as zeros.

There is also `nativeExport(handle, dir)` → `AudioEngine::export_session`: it stops the
audio device, writes every sample as a float32 WAV plus a `session.json` into `dir`, then
restarts the device (see docs/engine.md — Export).

## Android / multiplatform plan

The C++ engine is already platform-agnostic. `engine/CMakeLists.txt` guards the
libc++-header workaround with `if(APPLE AND NOT ANDROID)`, so the engine builds cleanly
under the Android NDK. The `TL_BUILD_TESTS` option skips gtest when targeting Android.

Adding Android support requires:

1. Enable `:app` in `settings.gradle.kts`.
2. Add `androidMain` source set to `shared/build.gradle.kts` with an `OboeIO` impl of
   `engine/src/platform/audio_io.h`.
3. Wire `app/src/main/` to load `libtap_looper_bridge.so` (same JNI surface as desktop).
4. The Compose UI in `shared/src/commonMain/` compiles for Android without changes.
