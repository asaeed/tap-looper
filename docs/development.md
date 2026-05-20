# Development

## One-time environment setup

### JDK 21

Kotlin 2.0.x requires JDK 21. JDK 25 (current Homebrew default) causes a version-string
parse failure and breaks the build.

```bash
brew install openjdk@21

# Expose java on PATH (adjust if ~/.local/bin is not on your PATH)
ln -sf /opt/homebrew/opt/openjdk@21/libexec/openjdk.jdk/Contents/Home/bin/java \
       ~/.local/bin/java

# Verify
java -version    # should print openjdk version "21.x.x"
```

Gradle picks up the JDK via the toolchain declaration in `desktop/build.gradle.kts`
(`jvmToolchain(21)`) and resolves it through Gradle's Java toolchain provisioning.

### macOS Command Line Tools

Required for Clang and CMake. Install if not already present:

```bash
xcode-select --install
```

CMake is bundled with Command Line Tools on recent macOS; verify with `cmake --version`.
If you have Xcode installed, CMake may also be found at
`/Applications/Xcode.app/Contents/Developer/usr/bin/cmake`.

## Build cycle

### Full build + run (most common)

```bash
./gradlew :desktop:run
```

This triggers the full chain: CMake rebuild of `libtap_looper_bridge.dylib` if any file
under `engine/src/` or `desktop/src/jvmMain/cpp/` changed (Gradle tracks these via the
`buildNativeLib` task's `inputs.files`), then the Kotlin/Compose build, then launches the
app. Gradle task dependency: `processResources` → `collectNativeLib` → `buildNativeLib`.

### Build without launching

```bash
./gradlew :desktop:build
```

### UI-only build (no native compile)

```bash
./gradlew :shared:build
```

Useful for type-checking Kotlin changes without waiting for the C++ compile.

### Standalone engine build + tests

The engine builds and tests independently of Gradle. Use this for fast C++ iteration:

```bash
cd engine
cmake -S . -B build
cmake --build build
ctest --test-dir build
```

Test sources are in `engine/test/`. gtest 1.14.0 is fetched via CMake `FetchContent` on
the first build.

## Gradle versions

Gradle wrapper is pinned to 8.7 in `gradle/wrapper/gradle-wrapper.properties`. Plugin
versions are declared centrally in the root `build.gradle.kts`:

| Plugin | Version |
|--------|---------|
| `kotlin.multiplatform` | 2.0.10 |
| `org.jetbrains.compose` | 1.7.0 |
| `kotlin.plugin.compose` | 2.0.10 |

Do not upgrade `kotlin.multiplatform` above 2.0.x without also verifying JDK compatibility.

## Known macOS CMakeLists quirk

`engine/CMakeLists.txt` has this block:

```cmake
if(APPLE AND NOT ANDROID)
    execute_process(COMMAND xcrun --show-sdk-path ...)
    if(... AND EXISTS "${TL_MACOS_SDK_PATH}/usr/include/c++/v1")
        add_compile_options(-isystem "${TL_MACOS_SDK_PATH}/usr/include/c++/v1")
    endif()
endif()
```

On this machine `/usr/bin/clang++` (the Command Line Tools compiler) does not
automatically locate the libc++ headers. The explicit `-isystem` path is required.
Do not remove this block. It is guarded by `NOT ANDROID` so it does not affect Android NDK
builds.

## Troubleshooting

### Exit code 137 (SIGKILL) on `./gradlew :desktop:run`

A previous Tap Looper JVM process is holding the audio device. Kill it and re-run:

```bash
pgrep -af "openjdk.*MainKt" | awk '{print $1}' | xargs kill
./gradlew :desktop:run
```

Or more bluntly:

```bash
pkill -f tap-looper; ./gradlew :desktop:run
```

### Exit code 144

Harmless background-task disconnect that occurs when running via the Claude Code agent
harness. The process was terminated by the harness, not by a bug. Ignore.

### `ClassNotFoundException: com.taplooper.desktop.MainKt`

`Main.kt` is in the wrong source set. The `desktop` module uses `kotlin("jvm")` (not
`kotlin("multiplatform")`), which expects sources at `src/main/kotlin/`, not
`src/jvmMain/kotlin/`. If you move or create a file in `src/jvmMain/kotlin/` under the
`desktop` module, the class will not be found at runtime.

Correct path: `desktop/src/main/kotlin/com/taplooper/desktop/Main.kt`

### Engine changes not reflected at runtime

Verify that `desktop/build.gradle.kts`'s `buildNativeLib` task lists `engine/src/` in
its inputs:

```kotlin
inputs.files(
    fileTree(cppDir) { exclude("build/**") },
    fileTree(file("../engine/src")),
    file("../engine/CMakeLists.txt"),
)
```

If this is intact, Gradle will detect source changes and re-run CMake. If CMake itself
is skipping the rebuild, delete `desktop/src/jvmMain/cpp/build/` and run again.

### First build is slow (~500 MB download)

The first `./gradlew :desktop:run` downloads Compose Multiplatform dependencies and the
Skia native libraries for the current OS/arch. Subsequent builds use the Gradle cache.

### Microphone permission denied

macOS prompts once. If you denied it, go to System Settings → Privacy & Security →
Microphone and re-enable the app. The engine will initialise but the input buffer will
be silent until permission is granted.

## Adding a new engine command

When adding a command that does not affect `EngineState.samples` or `EngineState.banks`
layout, only steps 1–4 are needed. If the new command affects what is marshalled over
JNI, all five steps are required.

1. **C++ enum** — add a new value to the `CommandType` enum at the end of the list in
   `engine/src/commands.h`.

2. **C++ implementation** — add a `case CommandType::YourCommand:` block in the `apply()`
   method in `engine/src/audio_engine.cpp`.

3. **Kotlin constant** — add a matching `const val YourCommand = <ordinal>` to
   `shared/src/commonMain/kotlin/com/taplooper/bridge/CommandType.kt`. The ordinal must
   match the C++ enum position (zero-indexed, in declaration order).

4. **ViewModel helper** — add a `fun yourCommand(...)` method to
   `shared/src/commonMain/kotlin/com/taplooper/domain/AppViewModel.kt` that calls
   `bridge.submit(CT.YourCommand, ...)`.

5. **JNI marshalling** (only if the command changes `EngineState`) — update the
   `nativeSnapshot` function in `desktop/src/jvmMain/cpp/bridge.cpp` to write the new
   field(s), and update `EngineBridge.kt::snapshot()` to read them back. Also update
   the relevant Kotlin data class in
   `shared/src/commonMain/kotlin/com/taplooper/bridge/EngineState.kt`.
   Array size constants in `EngineBridge.kt` (`longs` and `floats` allocation) must stay
   in sync with `bridge.cpp`'s `need_longs`/`need_floats` checks.
   See `docs/architecture.md` — JNI marshalling format for the full slot layout.
