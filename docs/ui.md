# UI Reference

All Compose UI code lives in `shared/src/commonMain/kotlin/com/taplooper/ui/`. Because it
is in `commonMain`, it compiles identically for the JVM desktop target and the future
Android target.

## Module structure

```
shared/src/commonMain/kotlin/com/taplooper/
  bridge/
    CommandType.kt        Integer constants matching C++ CommandType enum ordinals
    EngineBridge.kt       JNI wrapper; exposes submit() and snapshot()
    EngineState.kt        Kotlin data classes: EngineStateKt, BankStateKt, SampleStateKt
  domain/
    AppViewModel.kt       Owns EngineBridge, drives poll coroutine, exposes StateFlow
  ui/
    TapLooperScreen.kt    Root composable; owns AppViewModel and page/modal state
    theme/
      NeonArcade.kt       TLTheme data class + LocalTLTheme CompositionLocal
    components/
      TopBar.kt
      BankGrid.kt
      Bank.kt
      BankHeader.kt
      SampleBox.kt
      SampleModal.kt
      VolumeStrip.kt
    gestures/
      SampleBoxGestures.kt  tapLongPressDrag Modifier extension

desktop/src/main/kotlin/com/taplooper/desktop/
  Main.kt                 Compose application{} entry point; 360×720 dp window
```

Note: the desktop entry point is in `src/main/kotlin/` (JVM `kotlin("jvm")` source set),
not `src/jvmMain/kotlin/`. This is a deliberate choice; see `docs/development.md`.

## Component tree

```
TapLooperScreen(vm: AppViewModel)
  ├─ TopBar
  │    Mic level dot (decaying input peak; red when clipping)
  │    Status text (● Xs while recording / SAVED flash after export / blank)
  │    ⎘ CAPTURE button (targets focused bank = last-touched bank)
  │    SAVE button (exports WAVs + session.json to ~/Music/TapLooper/<timestamp>/)
  │    ▶ PLAY / ■ STOP toggle
  │    RESET button (LONG-PRESS only — a tap must not wipe the session)
  │    Page pips (2 dots, tappable)
  │
  ├─ BankGrid  (2×2 layout, shows 4 banks for current page)
  │    └─ Bank × 4
  │         ├─ BankHeader
  │         │    Bank label ("BANK 1" … "BANK 8")
  │         │    MUTE / ▶ ALL button (label changes with mode)
  │         │    SOLO button
  │         │    LOOP / ONE toggle
  │         ├─ Body (tap-to-record zone)
  │         │    └─ SampleAt × N  (one per sample in this bank)
  │         │         └─ SampleBox  (animated visual state)
  │         └─ VolumeStrip  (horizontal level indicator + drag control)
  │
  ├─ BottomBar  (two rows; row 2 visible only when master bar exists)
  │    Row 1: BPM display · ÷2 / ×2 (coarse beats-per-bar) · −0.1 / +0.1 BPM fine-tune
  │    Row 2: LAT display + −2 / +2 ms nudge · MET metronome toggle
  │
  └─ SampleModal  (overlaid Dialog, shown on long-press)
       Volume slider
       Reverse switch
       Repeat switch
       Speed selector (½ / 1 / 2)
       ADVANCED section (collapsible) → Start shift ±500ms slider
       DELETE button
```

## Gesture map

### Bank body (no sample under the touch point)

| Gesture | When | Action |
|---------|------|--------|
| Press down on empty area | Not recording | Start recording in this bank at the tapped position |
| …then move > 30 px horizontally | Same gesture | Cancel the just-started recording (`CancelRecording`) and change page instead |

Recording starts on `awaitFirstDown` (press-down event), not on pointer-up. The position
is normalised to the bank body dimensions and clamped so the resulting SampleBox cannot
straddle the bank edge (half-box margin, derived from `SampleBoxWidth/Height`).
Because most of the screen is empty bank space, a page swipe would otherwise always
kick off an unwanted recording — the same gesture handler watches for a horizontal
swipe and converts it into a page change, discarding the recording.

### Global (anywhere while recording)

| Gesture | Action |
|---------|--------|
| Press down anywhere | Stop recording |

This is handled by an `awaitFirstDown` gesture on the root `Box` in `TapLooperScreen`,
active only when `state.recording == true`. It fires on press-DOWN (not tap-up):
recording starts on down, so the loop span must end on down too — otherwise every
loop is stretched by the click duration.

### Sample box (`SampleAt`, via `tapLongPressDrag`)

| Gesture | LOOP mode | ONE-SHOT mode |
|---------|-----------|---------------|
| Tap | Toggle `enabled` (start/stop the loop) | Trigger from position 0 |
| Long-press (500 ms) | Open `SampleModal` | Open `SampleModal` |
| Drag (>12 px movement) | Reposition within bank; if dropped in different bank quadrant, move to that bank | Same |

Drag arbitration is implemented in `SampleBoxGestures.kt::tapLongPressDrag`. It uses
`awaitFirstDown` + a movement threshold of 12 px + a 500 ms timeout:
- If the pointer moves > 12 px before 500 ms elapses → drag.
- If 500 ms elapses without > 12 px movement → long-press (modal opens).
- If the pointer lifts before either threshold → tap.

Drag movement accumulates per-event `positionChange()` deltas — NOT
`position - down.position`, which double-counts when the box moves with the drag or
when multiple pointer events arrive within one frame.

While a drag is active, the dragged box, its bank cell, and its row are raised via
`Modifier.zIndex` (`BankGrid.kt` tracks `draggingBank`) so the box draws above the
other quadrants. On drop, the drag offset is held (`pendingDrop`) until the engine
round-trips the new position (or 250 ms), so the box doesn't flash back to its old
spot for a poll cycle. The drop's screen position is computed from the bank BODY's
origin (`bodyScreenOrigin`), not the whole bank — the header height used to shift
every drop upward.

The `down.consume()` call on `awaitFirstDown` prevents the bank body's
`awaitFirstDown(requireUnconsumed = true)` from also seeing the event, so a tap on a
sample never accidentally starts a new recording.

Cross-bank drop mapping uses the banks' actual geometry: each `Bank` reports its body's
screen rect (`onBodyRect`, sized with the bank constraints — the same basis sample
positions are normalised against), and `TapLooperScreen` keeps them in `bankBodyRects`.
On drop, the target is the bank whose rect contains the point (nearest rect centre as
fallback), and the position is normalised within that rect — the exact inverse of how
boxes are rendered, so the box lands precisely where it was dropped.

### Bank header buttons

| Button | Action |
|--------|--------|
| MUTE (LOOP mode) | Toggle `SetBankMute` |
| ▶ ALL (ONE-SHOT mode) | `FireAllInBank` — triggers every sample in the bank simultaneously |
| SOLO | Toggle `SetBankSolo`; when any bank is soloed, all non-soloed banks are silenced by the mixer |
| LOOP / ONE | Toggle `SetBankMode`; resets all samples in the bank to stopped |

### Volume strip

Tap or horizontal drag sets bank volume (0..1) proportional to x position within the
strip. Implemented with `detectTapGestures` + `detectHorizontalDragGestures` on a 12 dp
tall bar at the bottom of each bank.

### Page swipe

Horizontal drag of > 30 px anywhere on the screen (when not recording) advances or
retreats the page. Page 0 shows banks 1–4, page 1 shows banks 5–8. Active page is
indicated by the two pip dots in TopBar. Swipes starting on empty bank space are
handled by the bank body gesture (see above), which cancels the press-down recording
first.

### Keyboard (desktop)

Handled via `onPreviewKeyEvent` in `desktop/…/Main.kt`:

| Key | When | Action |
|-----|------|--------|
| any key | Recording | Stop recording |
| Space | Not recording | Toggle master PLAY / STOP |

## Sample modal

Opened by long-pressing any sample box. The modal receives the sample's current state
fields directly from `state.samples.find { it.id == id }` on every recompose — it does
NOT hold a captured snapshot. This is intentional: slider changes send commands to the
engine and the state round-trips back through the poll coroutine, keeping the slider
position reactive to the live engine value.

If the sample is deleted while the modal is open, the `find` returns null and a
`LaunchedEffect` closes the modal on the next frame.

Controls:
- **Volume**: Slider 0..1, shown as integer percentage.
- **Reverse**: Switch; non-destructive (PCM unchanged, read direction inverted in mixer).
- **Repeat**: Switch; sub-bar LOOP only — tiles the sample across the full bar.
- **Speed**: Segmented selector ½ / 1 / 2; uses linear interpolation in the mixer.
- **ADVANCED** (collapsible): Start shift slider ±500 ms (±24000 frames at 48 kHz). Rotates
  the playback entry point within the PCM; negative values advance the start.
- **DELETE**: Sends `DeleteSample`, then dismisses the modal.

## Theme

Defined in `shared/…/ui/theme/NeonArcade.kt` as a `TLTheme` data class provided via
`LocalTLTheme`.

| Property | Value |
|----------|-------|
| `background` | `#000000` |
| `text` | `#AAAAAA` |
| `recording` | `#FF4D00` (red-orange) |
| `pulseScale` | `1.10f` (playing boxes scale up 10%) |
| `pulseGlowDp` | `28.dp` (shadow elevation at peak pulse) |

Bank colors (index 0–7):

| Index | Hex | Label |
|-------|-----|-------|
| 0 | `#00FFD5` | cyan |
| 1 | `#FF00AA` | magenta |
| 2 | `#9D00FF` | violet |
| 3 | `#FF4D00` | red-orange |
| 4 | `#00FF66` | green |
| 5 | `#FFEA00` | yellow |
| 6 | `#00B0FF` | blue |
| 7 | `#FF005C` | pink |

## Visual states and animations

`SampleVisualState` (defined in `SampleBox.kt`) has four values:

| State | Visual |
|-------|--------|
| `Idle` | Subtle border at 50% alpha + 8% fill |
| `Playing` | Solid fill + matching border + glow shadow + BPM-synced pulse scale |
| `Recording` | Transparent fill + red-orange border + fast pulse (~3 Hz, independent) |
| `Muted` | No fill + 30% alpha border + 40% overall opacity |

Every box with recorded audio also draws a waveform thumbnail: the sample's 32-bin
peak envelope (from the engine snapshot), normalised to its own maximum, as mirrored
vertical bars — black at 35% on playing (solid fill) boxes, bank color at 45% otherwise.
The box footprint is defined by `SampleBoxWidth` × `SampleBoxHeight` (50×38 dp) in
`SampleBox.kt`; `Bank.kt` derives its clamp margins from the same constants.

The `playing` flag from the engine is solo/mute-aware: samples in banks silenced by a
solo elsewhere (or by their own mute) report `playing = false`, so boxes never pulse
while inaudible.

**BPM pulse**: A single `InfiniteTransition` is created in `TapLooperScreen` for the
current BPM and produces a `pulsePhase` value (0..1) that is passed all the way down to
each `SampleBox`. All playing boxes share this phase value, so they pulse in sync. The
pulse drives both scale (`1.0 ↔ 1.10`) and glow elevation (`0 ↔ 28.dp`) via a sine curve:

```kotlin
val pulseAmount = 0.5f + 0.5f * sin(phase * 2f * PI.toFloat())
val scaleTarget = 1f + (theme.pulseScale - 1f) * pulseAmount
val elevation   = theme.pulseGlowDp * pulseAmount
```

**Recording pulse**: Each recording box runs its own `InfiniteTransition` at 300 ms period
(~3.3 Hz), independent of BPM, for a fast eye-catching flash.

`SampleAt` (the positioned wrapper inside `Bank`) derives the visual state from the
sample's live engine state:
```kotlin
val visualState = when {
    s.recording -> SampleVisualState.Recording
    muted       -> SampleVisualState.Muted
    oneShot && !s.playing -> SampleVisualState.Idle
    s.playing && !muted   -> SampleVisualState.Playing
    else -> SampleVisualState.Idle
}
```
