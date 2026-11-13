package com.taplooper.ui

import androidx.compose.animation.core.*
import androidx.compose.foundation.background
import androidx.compose.foundation.gestures.awaitEachGesture
import androidx.compose.foundation.gestures.awaitFirstDown
import androidx.compose.foundation.gestures.detectHorizontalDragGestures
import androidx.compose.foundation.layout.*
import androidx.compose.material3.Surface
import androidx.compose.runtime.*
import androidx.compose.ui.Modifier
import androidx.compose.ui.geometry.Rect
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.input.pointer.pointerInput
import com.taplooper.domain.AppViewModel
import com.taplooper.ui.components.*
import com.taplooper.ui.theme.LocalTLTheme
import com.taplooper.ui.theme.TLTheme
import kotlin.math.abs
import kotlinx.coroutines.delay

@Composable
fun TapLooperScreen(vm: AppViewModel) {
    val state by vm.state.collectAsState()
    var page by remember { mutableIntStateOf(0) }
    var modalSampleId by remember { mutableStateOf<Long?>(null) }
    // Each bank body's screen rect (reported by Bank) — used to map drag drops
    // to the exact bank + position instead of approximate screen quadrants.
    val bankBodyRects = remember { mutableStateMapOf<Int, Rect>() }

    // Flash "SAVED" in the TopBar for a moment after each export
    val exportDir by vm.lastExportDir.collectAsState()
    var showSaved by remember { mutableStateOf(false) }
    LaunchedEffect(exportDir) {
        if (exportDir != null) {
            showSaved = true
            delay(2500)
            showSaved = false
        }
    }

    val pulseHz = if (state.bpm > 0) state.bpm / 60f else 1f
    val durationMs = ((1000f / pulseHz).coerceAtLeast(150f)).toInt()
    val pulseTransition = rememberInfiniteTransition(label = "global-pulse")
    val pulsePhase by pulseTransition.animateFloat(
        initialValue = 0f, targetValue = 1f,
        animationSpec = infiniteRepeatable(tween(durationMs, easing = LinearEasing), RepeatMode.Restart),
        label = "phase"
    )

    val theme = TLTheme()
    CompositionLocalProvider(LocalTLTheme provides theme) {
        Surface(Modifier.fillMaxSize(), color = Color.Black) {
            Box(
                Modifier
                    .fillMaxSize()
                    .pointerInput(state.recording) {
                        if (state.recording) {
                            // Stop on press-DOWN, not tap-up: recording starts on
                            // down, so the loop span must end on down too.
                            awaitEachGesture {
                                awaitFirstDown(requireUnconsumed = false)
                                vm.stopRecording()
                            }
                        }
                    }
                    .pointerInput(state.recording, page) {
                        if (!state.recording) {
                            detectHorizontalDragGestures(onDragEnd = {}) { _, dragAmount ->
                                if (abs(dragAmount) > 30f) {
                                    page = if (dragAmount < 0) (page + 1).coerceAtMost(1)
                                           else (page - 1).coerceAtLeast(0)
                                }
                            }
                        }
                    }
            ) {
                Column(Modifier.fillMaxSize()) {
                    TopBar(
                        recording = state.recording,
                        recordingDurationMs = state.recordingDurationFrames * 1000 / 48000,
                        currentPage = page,
                        masterPlaying = state.masterPlaying,
                        inputPeak = state.inputPeak,
                        showSaved = showSaved,
                        onCapture = { vm.captureLastLoop(state.focusedBank) },
                        onSave = { vm.exportSession() },
                        onReset = { vm.reset() },
                        onTogglePlay = { vm.setMasterPlaying(!state.masterPlaying) },
                        onPageTap = { p -> if (!state.recording) page = p },
                    )
                    Box(Modifier.weight(1f).fillMaxWidth()) {
                        BankGrid(
                            page = page,
                            banks = state.banks,
                            samples = state.samples,
                            bpm = state.bpm,
                            recording = state.recording,
                            focusedBank = state.focusedBank,
                            bankColors = theme.bankColors,
                            pulsePhase = pulsePhase,
                        ) { bankId ->
                            BankCallbacks(
                                onTapEmpty = { x, y -> vm.startRecording(bankId, x, y) },
                                onAction = {
                                    val isOneShot = state.banks[bankId].mode == 1
                                    if (isOneShot) vm.fireAllInBank(bankId)
                                    else vm.setBankMute(bankId, !state.banks[bankId].muted)
                                },
                                onToggleMode = {
                                    val cur = state.banks[bankId].mode == 1
                                    vm.setBankMode(bankId, !cur)
                                },
                                onToggleSolo = { vm.setBankSolo(bankId, !state.banks[bankId].soloed) },
                                onSampleTap = { s -> vm.tapSample(s.id) },
                                onSampleLongPress = { s -> modalSampleId = s.id },
                                // A horizontal swipe that started on empty bank space kicked
                                // off a recording on press-down — discard it and page instead.
                                onSwipe = { dir ->
                                    vm.cancelRecording()
                                    page = (page + dir).coerceIn(0, 1)
                                },
                                onSampleDragEnd = { s, off ->
                                    val pageBanks = (page * 4) until (page * 4 + 4)
                                    val target = pageBanks.firstOrNull {
                                        bankBodyRects[it]?.contains(off) == true
                                    } ?: pageBanks.minByOrNull {
                                        val c = bankBodyRects[it]?.center
                                        if (c == null) Float.MAX_VALUE
                                        else (off.x - c.x) * (off.x - c.x) + (off.y - c.y) * (off.y - c.y)
                                    } ?: s.bank
                                    bankBodyRects[target]?.let { r ->
                                        val nx = ((off.x - r.left) / r.width).coerceIn(0f, 1f)
                                        val ny = ((off.y - r.top) / r.height).coerceIn(0f, 1f)
                                        vm.moveSample(s.id, target, nx, ny)
                                    }
                                },
                                onBodyRect = { r -> bankBodyRects[bankId] = r },
                                onVolumeChange = { v -> vm.setBankVolume(bankId, v) },
                            )
                        }
                    }
                    BottomBar(
                        bpm = state.bpm,
                        beatsPerBar = state.beatsPerBar,
                        masterBarFrames = state.masterBarFrames,
                        extraLatencyFrames = state.extraLatencyFrames,
                        metronome = state.metronome,
                        onChangeBeats = { v -> vm.setBeatsPerBar(v) },
                        onChangeMasterBarFrames = { v -> vm.setMasterBarFrames(v) },
                        onChangeExtraLatency = { v -> vm.setExtraLatencyFrames(v) },
                        onToggleMetronome = { vm.setMetronome(!state.metronome) },
                    )
                }
            }

            modalSampleId?.let { id ->
                val s = state.samples.find { it.id == id }
                if (s == null) {
                    // sample was deleted; close the modal
                    LaunchedEffect(Unit) { modalSampleId = null }
                } else {
                    SampleModal(
                        label = "Bank ${s.bank + 1} / Sample ${s.id}",
                        volume = s.volume,
                        reverse = s.reverse,
                        speed = s.speed,
                        startOffsetFrames = s.startOffsetFrames,
                        repeat = s.repeat,
                        color = theme.bankColors[s.bank],
                        onVolumeChange = { vm.setSampleVolume(s.id, it) },
                        onReverseChange = { vm.setSampleReverse(s.id, it) },
                        onSpeedChange = { vm.setSampleSpeed(s.id, it) },
                        onStartOffsetChange = { vm.setSampleStartOffset(s.id, it) },
                        onRepeatChange = { vm.setSampleRepeat(s.id, it) },
                        onDelete = { vm.deleteSample(s.id) },
                        onDismiss = { modalSampleId = null },
                    )
                }
            }
        }
    }
}

