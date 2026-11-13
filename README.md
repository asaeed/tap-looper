# Tap Looper

Tap Looper is a real-time live-looping instrument. Touch an empty bank to start recording
from the microphone; tap anywhere to stop. The first loop you record sets the master bar
length, and every subsequent recording snaps to that grid. Samples live in banks arranged
in a 2x2 pad grid (two pages = eight banks total). You can drag samples between banks,
flip between LOOP and ONE-SHOT mode non-destructively, mute, solo, reverse, and adjust
playback speed — all while the engine keeps running.

Sample boxes show a waveform thumbnail, a metronome click (MET) can mark the beat, the
mic level dot in the top bar shows live input, and SAVE exports every sample as a
float32 WAV plus a `session.json` to `~/Music/TapLooper/<timestamp>/`. On desktop,
space toggles play/stop and any key stops an active recording. RESET requires a
long-press.

## Quick start (macOS)

```bash
# 1. Install JDK 21 (Kotlin 2.0.x requires it; JDK 25 breaks version parsing)
brew install openjdk@21

# 2. Expose java on PATH so the Gradle wrapper finds it
ln -sf /opt/homebrew/opt/openjdk@21/libexec/openjdk.jdk/Contents/Home/bin/java \
       ~/.local/bin/java

# 3. Build and run
./gradlew :desktop:run
```

On first launch macOS will prompt for microphone permission — grant it.
The first Gradle build downloads Compose Multiplatform + Skia native libs (~500 MB).

## Run the engine tests

The C++ unit tests live in `engine/` and build independently of Gradle:

```bash
cd engine
cmake -S . -B build
cmake --build build
ctest --test-dir build
```

## Project tree

```
tap-looper/
  engine/          C++17 audio engine (portable, no platform deps)
  shared/          Kotlin Multiplatform — Compose UI + JNI bridge
  desktop/         JVM entry point, miniaudio I/O, JNI native lib build
  app/             Future Android shell (deferred, not yet active)
  docs/            Reference documentation (see below)
  gradle/          Wrapper files (Gradle 8.7)
```

## Documentation

| File | What it covers |
|------|----------------|
| `docs/architecture.md` | Layer diagram, module responsibilities, threading model, data flow, JNI marshalling format, Android plan |
| `docs/engine.md` | C++ engine deep-dive: sample model, recording + trim pipeline, latency compensation, mixer branches, command set |
| `docs/ui.md` | Compose UI reference: component tree, gesture map, sample modal, theme, visual states |
| `docs/development.md` | Build/run/troubleshoot: env setup, build cycle, known quirks, how to add a new command |

Historical design specs and implementation plans are in `docs/superpowers/` — those are
read-only records of how the project was built and are not updated as the codebase evolves.
