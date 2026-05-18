# Tap Looper Multiplatform Plan

> Incremental plan that converts the project (currently Phase 2 complete — engine + 30 tests passing on macOS host) to Compose Multiplatform targeting **macOS Desktop first**, then Android.

**Goal:** A runnable interactive Mac desktop app driving the existing C++ engine, with the same Compose UI ready to wrap as Android later.

**Architecture:** `engine` (C++, unchanged) ← JNI (per-platform) ← `shared` (Compose UI in commonMain, Kotlin Multiplatform) → `desktop` (JVM entry point) and later → `app` (Android). Audio I/O is per-platform: miniaudio on Desktop, Oboe on Android.

**Tech stack additions vs. the original plan:**
- Kotlin Multiplatform with Compose Multiplatform plugin
- miniaudio (single-header C) for Mac/Win/Linux audio I/O
- Gradle wrapper (will bootstrap via `brew install gradle` once)

---

## Phases

| Phase | Deliverable | Tasks |
|---|---|---|
| **MP-A** | Multiplatform project structure + Desktop audio + JNI bridge | 7 |
| **MP-B** | Compose UI in shared module | 11 |
| **MP-C** | Desktop entry + verification on Mac | 3 |
| **MP-D** | Android shell (deferred until SDK installed) | 5 |

**Get to "runnable on Mac" after Phase MP-C** (21 tasks).

---

## File map (new + changed)

```
~/claude/tap-looper/
├── engine/                          ← unchanged from Phase 2 (30 tests passing)
├── gradle/wrapper/                  ← NEW: gradle wrapper files
├── gradlew, gradlew.bat             ← NEW
├── settings.gradle.kts              ← MODIFIED: include :shared, :desktop, :app
├── build.gradle.kts                 ← MODIFIED: KMP + Compose plugin versions
├── gradle.properties                ← MODIFIED: add Compose & KMP versions
├── shared/                          ← NEW: shared UI module
│   ├── build.gradle.kts
│   └── src/
│       ├── commonMain/kotlin/com/taplooper/
│       │   ├── bridge/EngineBridge.kt           ← Kotlin facade (loads native lib)
│       │   ├── bridge/EngineState.kt            ← Kotlin mirrors
│       │   ├── bridge/CommandType.kt
│       │   ├── domain/AppViewModel.kt
│       │   ├── ui/TapLooperScreen.kt
│       │   ├── ui/components/{TopBar,BottomBar,BankGrid,Bank,BankHeader,
│       │   │                  SampleBox,VolumeStrip,SampleModal}.kt
│       │   ├── ui/gestures/SampleBoxGestures.kt
│       │   └── ui/theme/NeonArcade.kt
│       ├── androidMain/kotlin/com/taplooper/bridge/
│       │   └── NativeLib.android.kt             ← actual System.loadLibrary on Android
│       └── desktopMain/kotlin/com/taplooper/bridge/
│           └── NativeLib.desktop.kt             ← System.loadLibrary on JVM Desktop
├── desktop/                         ← NEW: Desktop entry point
│   ├── build.gradle.kts
│   └── src/jvmMain/
│       ├── kotlin/Main.kt                       ← Compose Window + screen
│       ├── cpp/
│       │   ├── CMakeLists.txt
│       │   ├── miniaudio.h                      ← vendored single-header
│       │   ├── audio_io_miniaudio.cpp           ← AudioIO impl on top of miniaudio
│       │   └── bridge.cpp                       ← JNI implementations (same shape as Android)
│       └── resources/                           ← bundled native libs after build
├── app/                             ← NEW for Android (deferred): depends on :shared
│   └── src/main/cpp/                ← Android JNI (uses Oboe instead of miniaudio)
└── docs/superpowers/                ← unchanged
```

---

# Phase MP-A — Multiplatform project structure + Desktop audio

### Task MP-A1: Install Gradle and bootstrap the wrapper

**Files:**
- Create: `~/claude/tap-looper/gradlew`, `~/claude/tap-looper/gradlew.bat`, `~/claude/tap-looper/gradle/wrapper/gradle-wrapper.jar`, `~/claude/tap-looper/gradle/wrapper/gradle-wrapper.properties`

- [ ] **Step 1**: Check if gradle is installed: `which gradle`. If not present, install via Homebrew:
```bash
brew install gradle
```

- [ ] **Step 2**: Bootstrap the wrapper at a known-good version:
```bash
cd ~/claude/tap-looper && gradle wrapper --gradle-version 8.7
```

- [ ] **Step 3**: Verify the wrapper works:
```bash
./gradlew --version
```
Expected: Gradle 8.7 reported.

- [ ] **Step 4**: Commit:
```bash
git add gradlew gradlew.bat gradle/ && git commit -m "build: add Gradle 8.7 wrapper"
```

---

### Task MP-A2: Replace root gradle config with KMP + Compose Multiplatform

**Files:**
- Modify: `~/claude/tap-looper/settings.gradle.kts`
- Modify: `~/claude/tap-looper/build.gradle.kts`
- Modify: `~/claude/tap-looper/gradle.properties`

- [ ] **Step 1**: Replace `settings.gradle.kts`:
```kotlin
pluginManagement {
    repositories {
        google {
            mavenContent {
                includeGroupAndSubgroups("androidx")
                includeGroupAndSubgroups("com.android")
                includeGroupAndSubgroups("com.google")
            }
        }
        mavenCentral()
        gradlePluginPortal()
    }
}
dependencyResolutionManagement {
    repositoriesMode.set(RepositoriesMode.FAIL_ON_PROJECT_REPOS)
    repositories {
        google { mavenContent { includeGroupAndSubgroups("androidx"); includeGroupAndSubgroups("com.android"); includeGroupAndSubgroups("com.google") } }
        mavenCentral()
    }
}

rootProject.name = "tap-looper"

include(":shared")
include(":desktop")
// :app (Android) will be added in Phase MP-D once SDK is installed
```

- [ ] **Step 2**: Replace root `build.gradle.kts` with empty plugin pinning (versions come from the KMP/Compose plugins in subprojects):
```kotlin
plugins {
    // Plugin versions declared centrally; modules apply without specifying versions.
    id("org.jetbrains.kotlin.multiplatform") version "2.0.20" apply false
    id("org.jetbrains.compose") version "1.7.0" apply false
    id("org.jetbrains.kotlin.plugin.compose") version "2.0.20" apply false
}
```

- [ ] **Step 3**: Update `gradle.properties`:
```
org.gradle.jvmargs=-Xmx4g
org.gradle.parallel=true
org.gradle.caching=true
kotlin.code.style=official
```

- [ ] **Step 4**: Verify gradle can resolve plugins:
```bash
./gradlew help --no-daemon
```
Expected: no errors. (No modules to build yet; just confirms plugin resolution.)

- [ ] **Step 5**: Commit:
```bash
git add settings.gradle.kts build.gradle.kts gradle.properties && \
git commit -m "build: convert root config to KMP + Compose Multiplatform"
```

---

### Task MP-A3: Create `shared` module skeleton (commonMain + desktopMain)

**Files:**
- Create: `~/claude/tap-looper/shared/build.gradle.kts`
- Create: `~/claude/tap-looper/shared/src/commonMain/kotlin/com/taplooper/Placeholder.kt`
- Create: `~/claude/tap-looper/shared/src/desktopMain/kotlin/com/taplooper/Platform.desktop.kt`

- [ ] **Step 1**: Write `shared/build.gradle.kts`:
```kotlin
plugins {
    kotlin("multiplatform")
    id("org.jetbrains.compose")
    id("org.jetbrains.kotlin.plugin.compose")
}

kotlin {
    jvm("desktop")
    sourceSets {
        commonMain.dependencies {
            implementation(compose.runtime)
            implementation(compose.foundation)
            implementation(compose.material3)
            implementation(compose.ui)
            implementation(compose.components.resources)
        }
        commonTest.dependencies {
            implementation(kotlin("test"))
        }
        val desktopMain by getting {
            dependencies {
                implementation(compose.desktop.currentOs)
            }
        }
    }
}
```

- [ ] **Step 2**: Create a placeholder file in commonMain to satisfy the build:
```bash
mkdir -p ~/claude/tap-looper/shared/src/commonMain/kotlin/com/taplooper
mkdir -p ~/claude/tap-looper/shared/src/desktopMain/kotlin/com/taplooper
```

Write `shared/src/commonMain/kotlin/com/taplooper/Placeholder.kt`:
```kotlin
package com.taplooper

internal const val PLACEHOLDER = "shared module loaded"
```

Write `shared/src/desktopMain/kotlin/com/taplooper/Platform.desktop.kt`:
```kotlin
package com.taplooper

internal val platformName: String = "Desktop JVM"
```

- [ ] **Step 3**: Build:
```bash
./gradlew :shared:build --no-daemon
```
Expected: builds, downloading Compose Multiplatform dependencies on first run. May take a few minutes the first time.

- [ ] **Step 4**: Commit:
```bash
git add shared/ && git commit -m "build: scaffold :shared module with commonMain + desktopMain"
```

---

### Task MP-A4: Add miniaudio + `AudioIO` desktop implementation

**Files:**
- Create: `~/claude/tap-looper/engine/src/platform/audio_io.h` (the interface, not yet created in Phase 2)
- Create: `~/claude/tap-looper/desktop/src/jvmMain/cpp/miniaudio.h` (vendored single-header from https://miniaud.io)
- Create: `~/claude/tap-looper/desktop/src/jvmMain/cpp/audio_io_miniaudio.cpp`

- [ ] **Step 1**: Write `engine/src/platform/audio_io.h`:
```cpp
#pragma once
#include "../audio_engine.h"

namespace tl {

// Platform-specific audio I/O. Owns the platform stream(s) and drives engine.process().
class AudioIO {
public:
    virtual ~AudioIO() = default;
    virtual bool start(AudioEngine& engine) = 0;
    virtual void stop() = 0;
};

} // namespace tl
```

- [ ] **Step 2**: Download miniaudio.h:
```bash
mkdir -p ~/claude/tap-looper/desktop/src/jvmMain/cpp
curl -fsSL https://raw.githubusercontent.com/mackron/miniaudio/0.11.21/miniaudio.h \
  -o ~/claude/tap-looper/desktop/src/jvmMain/cpp/miniaudio.h
```
(Version pin 0.11.21 — current as of May 2026; bump later if needed.)

- [ ] **Step 3**: Write `desktop/src/jvmMain/cpp/audio_io_miniaudio.cpp`:
```cpp
#define MINIAUDIO_IMPLEMENTATION
#include "miniaudio.h"
#include "audio_io.h"

#include <cstdio>
#include <memory>
#include <vector>

namespace tl {

class MiniaudioIO : public AudioIO {
public:
    bool start(AudioEngine& engine) override {
        engine_ = &engine;

        ma_device_config cfg = ma_device_config_init(ma_device_type_duplex);
        cfg.sampleRate         = kSampleRate;
        cfg.capture.format     = ma_format_f32;
        cfg.capture.channels   = kChannels;
        cfg.playback.format    = ma_format_f32;
        cfg.playback.channels  = kChannels;
        cfg.periodSizeInFrames = 256;       // ~5ms at 48kHz; tweak after measurement
        cfg.periods            = 2;
        cfg.dataCallback       = &OnAudio;
        cfg.pUserData          = this;

        if (ma_device_init(nullptr, &cfg, &device_) != MA_SUCCESS) {
            std::fprintf(stderr, "miniaudio: device init failed\n");
            return false;
        }
        if (ma_device_start(&device_) != MA_SUCCESS) {
            std::fprintf(stderr, "miniaudio: device start failed\n");
            ma_device_uninit(&device_);
            return false;
        }
        started_ = true;
        return true;
    }

    void stop() override {
        if (started_) {
            ma_device_uninit(&device_);
            started_ = false;
        }
    }

private:
    static void OnAudio(ma_device* device, void* output, const void* input, ma_uint32 frames) {
        auto* self = static_cast<MiniaudioIO*>(device->pUserData);
        const float* in  = static_cast<const float*>(input);
        float*       out = static_cast<float*>(output);
        self->engine_->process(in, out, int(frames));
    }

    AudioEngine* engine_ = nullptr;
    ma_device device_{};
    bool started_ = false;
};

extern "C" AudioIO* tl_create_audio_io() {
    return new MiniaudioIO();
}

} // namespace tl
```

- [ ] **Step 4**: No build verification here — happens in Task MP-A5 once CMakeLists wires it up.

- [ ] **Step 5**: Commit:
```bash
git add engine/src/platform/audio_io.h desktop/src/jvmMain/cpp/ && \
git commit -m "feat(desktop): vendor miniaudio + AudioIO impl"
```

---

### Task MP-A5: CMake build that produces a JNI-loadable `.dylib` for Mac

**Files:**
- Create: `~/claude/tap-looper/desktop/src/jvmMain/cpp/CMakeLists.txt`
- Create: `~/claude/tap-looper/desktop/src/jvmMain/cpp/bridge.cpp`

- [ ] **Step 1**: Write `desktop/src/jvmMain/cpp/bridge.cpp` (same shape as Android's, but for Desktop JVM):
```cpp
#include <jni.h>
#include <memory>
#include "audio_engine.h"
#include "audio_io.h"
#include "commands.h"

using namespace tl;

extern "C" AudioIO* tl_create_audio_io();

struct EngineHandle {
    std::unique_ptr<AudioEngine> engine = std::make_unique<AudioEngine>();
    std::unique_ptr<AudioIO> io { tl_create_audio_io() };
};

#define JF(retval, name) extern "C" JNIEXPORT retval JNICALL Java_com_taplooper_bridge_EngineBridge_##name

JF(jlong, nativeCreate)(JNIEnv*, jobject) {
    return reinterpret_cast<jlong>(new EngineHandle());
}
JF(void, nativeStart)(JNIEnv*, jobject, jlong h) {
    auto* eh = reinterpret_cast<EngineHandle*>(h);
    eh->io->start(*eh->engine);
}
JF(void, nativeStop)(JNIEnv*, jobject, jlong h) {
    auto* eh = reinterpret_cast<EngineHandle*>(h);
    eh->io->stop();
}
JF(void, nativeDestroy)(JNIEnv*, jobject, jlong h) {
    delete reinterpret_cast<EngineHandle*>(h);
}
JF(jboolean, nativeSubmit)(JNIEnv*, jobject, jlong h,
                           jint type, jlong sample_id, jint bank,
                           jfloat fx, jfloat fy, jfloat fvalue,
                           jint ivalue, jboolean bvalue) {
    auto* eh = reinterpret_cast<EngineHandle*>(h);
    Command c{};
    c.type = CommandType(type);
    c.sample_id = SampleId(sample_id);
    c.bank = BankId(bank);
    c.fx = fx; c.fy = fy; c.fvalue = fvalue;
    c.ivalue = ivalue; c.bvalue = bvalue;
    return eh->engine->submit(c) ? JNI_TRUE : JNI_FALSE;
}

JF(void, nativeSnapshot)(JNIEnv* env, jobject, jlong h,
                         jlongArray longs_out, jfloatArray floats_out) {
    auto* eh = reinterpret_cast<EngineHandle*>(h);
    EngineState st = eh->engine->snapshot();

    jsize need_longs  = 8 + kNumBanks * 2 + kMaxSamples * 8;
    jsize need_floats = 1 + kNumBanks + kMaxSamples * 4;
    if (env->GetArrayLength(longs_out) < need_longs ||
        env->GetArrayLength(floats_out) < need_floats) return;

    jlong* L = env->GetLongArrayElements(longs_out, nullptr);
    jfloat* F = env->GetFloatArrayElements(floats_out, nullptr);

    L[0] = jlong(st.version);
    L[1] = st.sample_count;
    L[2] = st.focused_bank;
    L[3] = st.master_bar_frames;
    L[4] = st.recording ? 1 : 0;
    L[5] = st.recording_duration_frames;
    L[6] = jlong(st.recording_sample_id);
    L[7] = st.beats_per_bar;

    F[0] = st.bpm;
    int Lp = 8, Fp = 1;
    for (int i = 0; i < kNumBanks; ++i) {
        L[Lp++] = int(st.banks[i].mode);
        L[Lp++] = st.banks[i].muted ? 1 : 0;
        F[Fp++] = st.banks[i].volume;
    }
    for (int i = 0; i < kMaxSamples; ++i) {
        if (i >= st.sample_count) {
            for (int k = 0; k < 8; ++k) L[Lp++] = 0;
            for (int k = 0; k < 4; ++k) F[Fp++] = 0.f;
            continue;
        }
        const SampleState& s = st.samples[i];
        L[Lp++] = jlong(s.id); L[Lp++] = s.bank;
        L[Lp++] = int(s.speed); L[Lp++] = s.enabled ? 1 : 0;
        L[Lp++] = s.playing ? 1 : 0; L[Lp++] = s.recording ? 1 : 0;
        L[Lp++] = s.recorded_length_frames; L[Lp++] = s.recorded_offset_in_bar;
        F[Fp++] = s.volume; F[Fp++] = s.reverse ? 1.f : 0.f;
        F[Fp++] = s.position_x; F[Fp++] = s.position_y;
    }

    env->ReleaseLongArrayElements(longs_out, L, 0);
    env->ReleaseFloatArrayElements(floats_out, F, 0);
}
```

- [ ] **Step 2**: Write `desktop/src/jvmMain/cpp/CMakeLists.txt`:
```cmake
cmake_minimum_required(VERSION 3.22)
project(tap_looper_bridge_desktop CXX C)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_C_STANDARD 11)

# Pull in the portable engine
add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/../../../../engine
                 ${CMAKE_BINARY_DIR}/engine)

find_package(JNI REQUIRED)

# miniaudio + bridge as a single shared lib
add_library(tap_looper_bridge SHARED
    audio_io_miniaudio.cpp
    bridge.cpp)

target_include_directories(tap_looper_bridge PRIVATE
    ${JNI_INCLUDE_DIRS}
    ${CMAKE_CURRENT_SOURCE_DIR}/../../../../engine/src
    ${CMAKE_CURRENT_SOURCE_DIR}/../../../../engine/src/platform)

target_link_libraries(tap_looper_bridge PRIVATE tl_engine)

if(APPLE)
    target_link_libraries(tap_looper_bridge PRIVATE "-framework CoreAudio" "-framework AudioUnit" "-framework CoreFoundation")
endif()

set_target_properties(tap_looper_bridge PROPERTIES
    OUTPUT_NAME "tap_looper_bridge"
    POSITION_INDEPENDENT_CODE ON)
```

- [ ] **Step 3**: Build the .dylib standalone first to catch any C++ errors:
```bash
cd ~/claude/tap-looper/desktop/src/jvmMain/cpp && \
cmake -S . -B build && cmake --build build
```
Expected: produces `desktop/src/jvmMain/cpp/build/libtap_looper_bridge.dylib`.

- [ ] **Step 4**: Commit:
```bash
cd ~/claude/tap-looper && git add desktop/ && \
git commit -m "feat(desktop): CMake build for JNI bridge .dylib using miniaudio"
```

---

### Task MP-A6: Kotlin `EngineBridge` + state mirror types in `:shared`

**Files:**
- Delete: `~/claude/tap-looper/shared/src/commonMain/kotlin/com/taplooper/Placeholder.kt`
- Create: `~/claude/tap-looper/shared/src/commonMain/kotlin/com/taplooper/bridge/EngineBridge.kt`
- Create: `~/claude/tap-looper/shared/src/commonMain/kotlin/com/taplooper/bridge/EngineState.kt`
- Create: `~/claude/tap-looper/shared/src/commonMain/kotlin/com/taplooper/bridge/CommandType.kt`

- [ ] **Step 1**: Write `bridge/CommandType.kt` (matches the C++ enum):
```kotlin
package com.taplooper.bridge

object CommandType {
    const val StartRecording   = 0
    const val StopRecording    = 1
    const val TapSample        = 2
    const val DeleteSample     = 3
    const val SetSampleVolume  = 4
    const val SetSampleReverse = 5
    const val SetSampleSpeed   = 6
    const val MoveSample       = 7
    const val SetBankMute      = 8
    const val SetBankVolume    = 9
    const val SetBankMode      = 10
    const val FireAllInBank    = 11
    const val CaptureLastLoop  = 12
    const val SetBeatsPerBar   = 13
}
```

- [ ] **Step 2**: Write `bridge/EngineState.kt`:
```kotlin
package com.taplooper.bridge

data class SampleStateKt(
    val id: Long,
    val bank: Int,
    val volume: Float,
    val reverse: Boolean,
    val speed: Int,
    val enabled: Boolean,
    val playing: Boolean,
    val recording: Boolean,
    val positionX: Float,
    val positionY: Float,
    val recordedLengthFrames: Int,
    val recordedOffsetInBar: Int,
)

data class BankStateKt(
    val id: Int,
    val mode: Int,
    val volume: Float,
    val muted: Boolean,
)

data class EngineStateKt(
    val banks: List<BankStateKt>,
    val samples: List<SampleStateKt>,
    val recording: Boolean,
    val recordingDurationFrames: Int,
    val recordingSampleId: Long,
    val focusedBank: Int,
    val masterBarFrames: Int,
    val beatsPerBar: Int,
    val bpm: Float,
    val version: Long,
)
```

- [ ] **Step 3**: Write `bridge/EngineBridge.kt`:
```kotlin
package com.taplooper.bridge

class EngineBridge {
    private val handle: Long = nativeCreate()

    fun start() = nativeStart(handle)
    fun stop()  = nativeStop(handle)
    fun close() = nativeDestroy(handle)

    fun submit(
        type: Int,
        sampleId: Long = 0,
        bank: Int = 0,
        fx: Float = 0f, fy: Float = 0f, fvalue: Float = 0f,
        ivalue: Int = 0, bvalue: Boolean = false
    ): Boolean = nativeSubmit(handle, type, sampleId, bank, fx, fy, fvalue, ivalue, bvalue)

    fun snapshot(): EngineStateKt {
        val longs = LongArray(8 + 8 * 2 + 64 * 8)
        val floats = FloatArray(1 + 8 + 64 * 4)
        nativeSnapshot(handle, longs, floats)
        val banks = (0 until 8).map { i ->
            val lp = 8 + i * 2
            val fp = 1 + i
            BankStateKt(id = i, mode = longs[lp].toInt(), muted = longs[lp+1] == 1L, volume = floats[fp])
        }
        val count = longs[1].toInt()
        val samples = (0 until count).map { i ->
            val lp = 8 + 8*2 + i * 8
            val fp = 1 + 8 + i * 4
            SampleStateKt(
                id = longs[lp],
                bank = longs[lp+1].toInt(),
                speed = longs[lp+2].toInt(),
                enabled = longs[lp+3] == 1L,
                playing = longs[lp+4] == 1L,
                recording = longs[lp+5] == 1L,
                recordedLengthFrames = longs[lp+6].toInt(),
                recordedOffsetInBar = longs[lp+7].toInt(),
                volume = floats[fp],
                reverse = floats[fp+1] != 0f,
                positionX = floats[fp+2],
                positionY = floats[fp+3],
            )
        }
        return EngineStateKt(
            banks = banks,
            samples = samples,
            recording = longs[4] == 1L,
            recordingDurationFrames = longs[5].toInt(),
            recordingSampleId = longs[6],
            focusedBank = longs[2].toInt(),
            masterBarFrames = longs[3].toInt(),
            beatsPerBar = longs[7].toInt(),
            bpm = floats[0],
            version = longs[0],
        )
    }

    private external fun nativeCreate(): Long
    private external fun nativeStart(h: Long)
    private external fun nativeStop(h: Long)
    private external fun nativeDestroy(h: Long)
    private external fun nativeSubmit(
        h: Long,
        type: Int, sampleId: Long, bank: Int,
        fx: Float, fy: Float, fvalue: Float,
        ivalue: Int, bvalue: Boolean
    ): Boolean
    private external fun nativeSnapshot(h: Long, longs: LongArray, floats: FloatArray)

    companion object {
        init { System.loadLibrary("tap_looper_bridge") }
    }
}
```

- [ ] **Step 4**: Delete the placeholder:
```bash
rm ~/claude/tap-looper/shared/src/commonMain/kotlin/com/taplooper/Placeholder.kt
```

- [ ] **Step 5**: Build:
```bash
./gradlew :shared:build --no-daemon
```
Expected: builds successfully.

- [ ] **Step 6**: Commit:
```bash
git add shared/ && git commit -m "feat(shared): EngineBridge + EngineState + CommandType in commonMain"
```

---

### Task MP-A7: `:desktop` module with Gradle CMake build + java.library.path wiring

**Files:**
- Create: `~/claude/tap-looper/desktop/build.gradle.kts`

- [ ] **Step 1**: Write `desktop/build.gradle.kts`:
```kotlin
plugins {
    kotlin("jvm")
    id("org.jetbrains.compose")
    id("org.jetbrains.kotlin.plugin.compose")
}

repositories {
    google()
    mavenCentral()
}

dependencies {
    implementation(project(":shared"))
    implementation(compose.desktop.currentOs)
}

// Build the C++ JNI lib via CMake on every assemble.
val buildNativeLib by tasks.registering(Exec::class) {
    val cppDir = file("src/jvmMain/cpp")
    val buildDir = file("$cppDir/build")
    workingDir = cppDir
    outputs.dir(buildDir)
    inputs.files(fileTree(cppDir) { exclude("build/**") })
    commandLine("sh", "-c",
        "cmake -S . -B build && cmake --build build")
}

// Copy the built .dylib into resources so System.loadLibrary picks it up at runtime.
val collectNativeLib by tasks.registering(Copy::class) {
    dependsOn(buildNativeLib)
    val cppBuild = file("src/jvmMain/cpp/build")
    from(cppBuild) {
        include("libtap_looper_bridge.dylib")
        include("libtap_looper_bridge.so")
        include("tap_looper_bridge.dll")
    }
    into(layout.buildDirectory.dir("native-libs"))
}

kotlin {
    jvmToolchain(17)
}

tasks.named("processResources") {
    dependsOn(collectNativeLib)
}

// Add native libs dir to java.library.path when running
tasks.withType<JavaExec>().configureEach {
    val libDir = file("build/native-libs").absolutePath
    systemProperty("java.library.path", libDir)
    environment("DYLD_LIBRARY_PATH", libDir)
}

compose.desktop {
    application {
        mainClass = "com.taplooper.desktop.MainKt"
        nativeDistributions {
            packageName = "TapLooper"
            packageVersion = "0.1.0"
        }
        // Distribute the native lib with the app
        jvmArgs("-Djava.library.path=" + file("build/native-libs").absolutePath)
    }
}
```

- [ ] **Step 2**: Verify Gradle resolves and the CMake task runs end-to-end:
```bash
./gradlew :desktop:collectNativeLib --no-daemon
```
Expected: builds the C++ lib via CMake, copies `libtap_looper_bridge.dylib` into `desktop/build/native-libs/`.

- [ ] **Step 3**: Commit:
```bash
git add desktop/build.gradle.kts && \
git commit -m "build(desktop): JVM + Compose Desktop + CMake-built native lib copy"
```

---

# Phase MP-B — Compose UI in shared module

Tasks MP-B1 through MP-B11 build out the Compose UI in `shared/src/commonMain/kotlin/com/taplooper/ui/`. The code is essentially what the original plan's Tasks 27–37 specified, with `androidx.lifecycle.ViewModel` replaced by a plain Kotlin class (Compose Multiplatform's lifecycle integration differs).

### Task MP-B1: `NeonArcade.kt` theme

Content matches original plan Task 27. File location: `shared/src/commonMain/kotlin/com/taplooper/ui/theme/NeonArcade.kt`.

### Task MP-B2: `TopBar.kt`

Original plan Task 28. Location: `shared/src/commonMain/kotlin/com/taplooper/ui/components/TopBar.kt`.

### Task MP-B3: `BottomBar.kt`

Original plan Task 29. Location: `shared/src/commonMain/kotlin/com/taplooper/ui/components/BottomBar.kt`.

### Task MP-B4: `VolumeStrip.kt`

Original plan Task 30. Location: `shared/src/commonMain/kotlin/com/taplooper/ui/components/VolumeStrip.kt`.

### Task MP-B5: `SampleBox.kt`

Original plan Task 31. Location: `shared/src/commonMain/kotlin/com/taplooper/ui/components/SampleBox.kt`.

### Task MP-B6: `BankHeader.kt`

Original plan Task 32. Location: `shared/src/commonMain/kotlin/com/taplooper/ui/components/BankHeader.kt`.

### Task MP-B7: `SampleBoxGestures.kt`

Original plan Task 33. Location: `shared/src/commonMain/kotlin/com/taplooper/ui/gestures/SampleBoxGestures.kt`.

### Task MP-B8: `SampleModal.kt`

Original plan Task 34. Location: `shared/src/commonMain/kotlin/com/taplooper/ui/components/SampleModal.kt`.

### Task MP-B9: `Bank.kt` composable

Original plan Task 35. Location: `shared/src/commonMain/kotlin/com/taplooper/ui/components/Bank.kt`.

### Task MP-B10: `BankGrid.kt`

Original plan Task 36. Location: `shared/src/commonMain/kotlin/com/taplooper/ui/components/BankGrid.kt`.

### Task MP-B11: `TapLooperScreen.kt` + `AppViewModel.kt`

Original plan Task 37, but `AppViewModel` becomes a plain class (no `androidx.lifecycle.ViewModel` in commonMain). Use a `CoroutineScope` instead of `viewModelScope`. Locations:
- `shared/src/commonMain/kotlin/com/taplooper/ui/TapLooperScreen.kt`
- `shared/src/commonMain/kotlin/com/taplooper/domain/AppViewModel.kt`

After this task, the shared module compiles with the full UI.

---

# Phase MP-C — Desktop entry point + Mac verification

### Task MP-C1: `Main.kt` Desktop entry

**Files:**
- Create: `~/claude/tap-looper/desktop/src/jvmMain/kotlin/com/taplooper/desktop/Main.kt`

```kotlin
package com.taplooper.desktop

import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.runtime.*
import androidx.compose.ui.Modifier
import androidx.compose.ui.unit.DpSize
import androidx.compose.ui.unit.dp
import androidx.compose.ui.window.Window
import androidx.compose.ui.window.application
import androidx.compose.ui.window.rememberWindowState
import com.taplooper.domain.AppViewModel
import com.taplooper.ui.TapLooperScreen

fun main() = application {
    val vm = remember { AppViewModel() }
    DisposableEffect(Unit) { onDispose { vm.dispose() } }

    val state = rememberWindowState(size = DpSize(360.dp, 720.dp))
    Window(
        onCloseRequest = ::exitApplication,
        title = "Tap Looper",
        state = state,
    ) {
        TapLooperScreen(vm)
    }
}
```

Note: `AppViewModel.dispose()` should call `bridge.stop()` and `bridge.close()`. The Phase MP-B11 task implementation should expose `dispose()` instead of overriding `onCleared()`.

- [ ] **Step 1**: Write the file.

- [ ] **Step 2**: Run:
```bash
./gradlew :desktop:run --no-daemon
```
Expected: A 360×720 window appears with the Tap Looper UI. Mic permission prompt appears (macOS asks for microphone access on first start; user must grant it via System Settings → Privacy & Security → Microphone if not auto-prompted).

- [ ] **Step 3**: Commit:
```bash
git add desktop/src/jvmMain/kotlin/ && \
git commit -m "feat(desktop): main entry — Compose Window + TapLooperScreen"
```

---

### Task MP-C2: Manual gesture verification

- [ ] **Step 1**: With the app running:
1. Click in an empty bank area — recording indicator appears in the top bar.
2. Click anywhere else — recording stops, a colored box appears at the original click location.
3. Click the box — toggles its enabled state (in LOOP mode).
4. Long-press the box (~500 ms) — sample modal opens with volume / reverse / speed / DELETE.
5. Drag a sample to a different bank quadrant — box moves and adopts the new bank's color.
6. Click the LOOP / ONE toggle on a bank — flips the mode; the bank's action button changes from "MUTE" to "▶ ALL".
7. Click "▶ ALL" on a ONE-SHOT bank — every sample in that bank fires.
8. Confirm BPM updates after the first recording, and the playing samples pulse at `bpm / 60` Hz.
9. Try ÷2 / ×2 BPM buttons — pulse rate changes correspondingly.

- [ ] **Step 2**: Note any defects in a `docs/superpowers/specs/verification-log.md`.

- [ ] **Step 3**: Commit any fixes; commit the verification log.

---

### Task MP-C3: Optional latency loopback measurement

- [ ] **Step 1**: Plug a 3.5 mm TRRS loopback cable from headphone out → mic in on the Mac (or use a USB interface in monitoring mode).

- [ ] **Step 2**: Record a 1-second loop containing a recognizable transient (a hand clap). Watch the resulting sample's PCM through the engine's debug logging (you can add a temporary `printf` in `audio_engine.cpp` to emit the first non-zero sample index, removed later).

- [ ] **Step 3**: Calculate round-trip latency from sample-index of the impulse.

- [ ] **Step 4**: Document the result in `verification-log.md`. Target: ≤ 20 ms round-trip.

- [ ] **Step 5**: Commit the log.

---

# Phase MP-D — Android shell (deferred until Android Studio + SDK + NDK installed)

Once Android tooling is installed:

### Task MP-D1: `:app` Android module skeleton

Create `app/build.gradle.kts` with the Android plugin, depends on `:shared`. Manifest, MainActivity.

### Task MP-D2: Android JNI bridge using Oboe

`app/src/main/cpp/CMakeLists.txt` + `bridge.cpp` + `audio_io_oboe.cpp` (same shape as desktop's miniaudio impl but using Oboe). Add Oboe via prefab.

### Task MP-D3: `NativeLib.android.kt` actual

In `shared/src/androidMain/`, the actual `System.loadLibrary("tap_looper_bridge")` lives — same library name on both platforms.

### Task MP-D4: Manifest + MainActivity

Standard Android entry hosting `TapLooperScreen` from `:shared`.

### Task MP-D5: Install + on-device verification

```bash
./gradlew :app:installDebug
```

Verify the same gesture map on the phone. Document in verification log.

---

## Self-review notes

- **Single source of truth for UI**: All composables live in `:shared:commonMain`. Both `:desktop` and `:app` consume them — no duplication.
- **Single source of truth for engine**: All audio logic lives in `engine/`. Both `:desktop` and `:app` link it via per-platform JNI bridges that share the same Kotlin facade (`EngineBridge` in commonMain).
- **Latency**: audio path is pure C++ on both platforms (miniaudio on desktop, Oboe on Android). JVM never runs on the audio thread.
- **macOS microphone permission**: the user is responsible for granting it once via System Settings on first run.
- **Known simplifications inherited from engine Phase 2**: mutex-protected StatePublisher, RT-thread allocations during first-recording and CaptureLastLoop. Both acceptable for MVP.
