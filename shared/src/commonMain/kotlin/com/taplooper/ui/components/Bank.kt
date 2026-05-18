package com.taplooper.ui.components

import androidx.compose.foundation.background
import androidx.compose.foundation.border
import androidx.compose.foundation.gestures.detectTapGestures
import androidx.compose.foundation.layout.*
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.runtime.Composable
import androidx.compose.ui.Modifier
import androidx.compose.ui.geometry.Offset
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.input.pointer.pointerInput
import androidx.compose.ui.unit.IntOffset
import androidx.compose.ui.unit.dp
import com.taplooper.bridge.BankStateKt
import com.taplooper.bridge.SampleStateKt
import com.taplooper.ui.gestures.tapLongPressDrag

@Composable
fun Bank(
    bankState: BankStateKt,
    samples: List<SampleStateKt>,
    color: Color,
    recording: Boolean,
    bpm: Float,
    onTapEmpty: (Float, Float) -> Unit,
    onAction: () -> Unit,
    onToggleMode: () -> Unit,
    onSampleTap: (SampleStateKt) -> Unit,
    onSampleLongPress: (SampleStateKt) -> Unit,
    onSampleDragStart: (SampleStateKt) -> Unit,
    onSampleDragMove: (SampleStateKt, Offset) -> Unit,
    onSampleDragEnd: (SampleStateKt, Offset) -> Unit,
    onVolumeChange: (Float) -> Unit,
    isFocused: Boolean,
) {
    BoxWithConstraints(
        Modifier
            .fillMaxSize()
            .border(if (isFocused) 1.dp else 0.dp, color.copy(alpha = 0.4f), RoundedCornerShape(8.dp))
            .background(Color(0xFF0A0A0A), RoundedCornerShape(8.dp))
    ) {
        val widthPx = constraints.maxWidth.toFloat()
        val heightPx = constraints.maxHeight.toFloat()
        Column(Modifier.fillMaxSize()) {
            BankHeader(
                label = "BANK ${bankState.id + 1}",
                color = color,
                oneShot = bankState.mode == 1,
                muted = bankState.muted,
                onToggleMode = onToggleMode,
                onAction = onAction,
            )
            Box(
                Modifier
                    .weight(1f)
                    .fillMaxWidth()
                    .pointerInput(recording) {
                        detectTapGestures { offset ->
                            if (recording) return@detectTapGestures
                            onTapEmpty(offset.x / widthPx, offset.y / heightPx)
                        }
                    }
            ) {
                samples.forEach { s ->
                    SampleAt(
                        s = s,
                        color = color,
                        bpm = bpm,
                        widthPx = widthPx,
                        heightPx = heightPx,
                        muted = bankState.muted,
                        oneShot = bankState.mode == 1,
                        onTap = { onSampleTap(s) },
                        onLongPress = { onSampleLongPress(s) },
                        onDragStart = { onSampleDragStart(s) },
                        onDragMove = { off -> onSampleDragMove(s, off) },
                        onDragEnd = { off -> onSampleDragEnd(s, off) },
                        enabled = !recording,
                    )
                }
            }
            VolumeStrip(volume = bankState.volume, color = color, onVolumeChange = onVolumeChange)
        }
    }
}

@Composable
private fun SampleAt(
    s: SampleStateKt,
    color: Color, bpm: Float,
    widthPx: Float, heightPx: Float,
    muted: Boolean, oneShot: Boolean,
    onTap: () -> Unit, onLongPress: () -> Unit,
    onDragStart: () -> Unit,
    onDragMove: (Offset) -> Unit,
    onDragEnd: (Offset) -> Unit,
    enabled: Boolean,
) {
    val state = when {
        s.recording -> SampleVisualState.Recording
        muted       -> SampleVisualState.Muted
        oneShot && !s.playing -> SampleVisualState.Idle
        s.playing && !muted   -> SampleVisualState.Playing
        else -> SampleVisualState.Idle
    }
    val xPx = (s.positionX * widthPx - 25f).toInt()
    val yPx = (s.positionY * heightPx - 19f).toInt()
    Box(
        Modifier
            .offset { IntOffset(xPx, yPx) }
            .tapLongPressDrag(
                onTap = onTap, onLongPress = onLongPress,
                onDragStart = onDragStart, onDragMove = onDragMove, onDragEnd = onDragEnd,
                enabled = enabled,
            )
    ) {
        SampleBox(state = state, color = color, bpm = bpm)
    }
}
