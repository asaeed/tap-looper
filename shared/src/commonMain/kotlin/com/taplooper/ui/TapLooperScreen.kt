package com.taplooper.ui

import androidx.compose.animation.core.*
import androidx.compose.foundation.background
import androidx.compose.foundation.gestures.detectHorizontalDragGestures
import androidx.compose.foundation.gestures.detectTapGestures
import androidx.compose.foundation.layout.*
import androidx.compose.material3.Surface
import androidx.compose.runtime.*
import androidx.compose.ui.Modifier
import androidx.compose.ui.geometry.Offset
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.input.pointer.pointerInput
import androidx.compose.ui.layout.onGloballyPositioned
import androidx.compose.ui.unit.IntSize
import com.taplooper.bridge.SampleStateKt
import com.taplooper.domain.AppViewModel
import com.taplooper.ui.components.*
import com.taplooper.ui.theme.LocalTLTheme
import com.taplooper.ui.theme.TLTheme
import kotlin.math.abs

@Composable
fun TapLooperScreen(vm: AppViewModel) {
    val state by vm.state.collectAsState()
    var page by remember { mutableIntStateOf(0) }
    var modalSample by remember { mutableStateOf<SampleStateKt?>(null) }
    var screenSize by remember { mutableStateOf(IntSize.Zero) }

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
                    .onGloballyPositioned { screenSize = it.size }
                    .pointerInput(state.recording) {
                        if (state.recording) {
                            detectTapGestures { vm.stopRecording() }
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
                        onCapture = { vm.captureLastLoop(state.focusedBank) },
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
                                onSampleLongPress = { s -> modalSample = s },
                                onSampleDragStart = { _ -> },
                                onSampleDragMove = { _, _ -> },
                                onSampleDragEnd = { s, off ->
                                    val target = bankIdAtOffset(off, screenSize, page)
                                    val (nx, ny) = localizeToBank(off, screenSize, target)
                                    vm.moveSample(s.id, target, nx, ny)
                                },
                                onVolumeChange = { v -> vm.setBankVolume(bankId, v) },
                            )
                        }
                    }
                    BottomBar(
                        bpm = state.bpm,
                        beatsPerBar = state.beatsPerBar,
                        masterBarFrames = state.masterBarFrames,
                        onChangeBeats = { v -> vm.setBeatsPerBar(v) },
                        onChangeMasterBarFrames = { v -> vm.setMasterBarFrames(v) },
                    )
                }
            }

            modalSample?.let { s ->
                SampleModal(
                    label = "Bank ${s.bank + 1} / Sample ${s.id}",
                    volume = s.volume,
                    reverse = s.reverse,
                    speed = s.speed,
                    color = theme.bankColors[s.bank],
                    startOffsetFrames = s.startOffsetFrames,
                    onVolumeChange = { vm.setSampleVolume(s.id, it) },
                    onReverseChange = { vm.setSampleReverse(s.id, it) },
                    onSpeedChange = { vm.setSampleSpeed(s.id, it) },
                    onStartOffsetChange = { vm.setSampleStartOffset(s.id, it) },
                    onDelete = { vm.deleteSample(s.id) },
                    onDismiss = { modalSample = null },
                )
            }
        }
    }
}

private fun bankIdAtOffset(o: Offset, screen: IntSize, page: Int): Int {
    val col = if (o.x < screen.width / 2f) 0 else 1
    val row = if (o.y < screen.height / 2f) 0 else 1
    return page * 4 + row * 2 + col
}
private fun localizeToBank(o: Offset, screen: IntSize, bankId: Int): Pair<Float, Float> {
    val col = (bankId % 4) % 2
    val row = ((bankId % 4) / 2)
    val w = screen.width / 2f
    val h = screen.height / 2f
    val x = (o.x - col * w) / w
    val y = (o.y - row * h) / h
    return x.coerceIn(0f, 1f) to y.coerceIn(0f, 1f)
}
