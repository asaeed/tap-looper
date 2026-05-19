package com.taplooper.ui.components

import androidx.compose.foundation.background
import androidx.compose.foundation.border
import androidx.compose.foundation.gestures.awaitEachGesture
import androidx.compose.foundation.gestures.awaitFirstDown
import androidx.compose.foundation.layout.*
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.runtime.*
import androidx.compose.ui.Modifier
import androidx.compose.ui.geometry.Offset
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.input.pointer.pointerInput
import androidx.compose.ui.layout.onGloballyPositioned
import androidx.compose.ui.layout.positionInRoot
import androidx.compose.ui.platform.LocalDensity
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
    pulsePhase: Float,
    isFocused: Boolean,
) {
    var bankScreenOrigin by remember { mutableStateOf(Offset.Zero) }

    BoxWithConstraints(
        Modifier
            .fillMaxSize()
            .border(if (isFocused) 1.dp else 0.dp, color.copy(alpha = 0.4f), RoundedCornerShape(8.dp))
            .background(Color(0xFF0A0A0A), RoundedCornerShape(8.dp))
            .onGloballyPositioned { coords -> bankScreenOrigin = coords.positionInRoot() }
    ) {
        val widthPx = constraints.maxWidth.toFloat()
        val heightPx = constraints.maxHeight.toFloat()
        val density = LocalDensity.current
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
                        awaitEachGesture {
                            val down = awaitFirstDown(requireUnconsumed = true)
                            if (recording) return@awaitEachGesture
                            // Clamp so box never straddles bank edge (Issue C)
                            val halfWPx = with(density) { 25.dp.toPx() }
                            val halfHPx = with(density) { 19.dp.toPx() }
                            val minX = halfWPx / widthPx
                            val maxX = 1f - halfWPx / widthPx
                            val minY = halfHPx / heightPx
                            val maxY = 1f - halfHPx / heightPx
                            val xNorm = (down.position.x / widthPx).coerceIn(minX, maxX)
                            val yNorm = (down.position.y / heightPx).coerceIn(minY, maxY)
                            onTapEmpty(xNorm, yNorm)
                        }
                    }
            ) {
                samples.forEach { s ->
                    SampleAt(
                        s = s,
                        color = color,
                        bpm = bpm,
                        pulsePhase = pulsePhase,
                        widthPx = widthPx,
                        heightPx = heightPx,
                        bankScreenOrigin = bankScreenOrigin,
                        muted = bankState.muted,
                        oneShot = bankState.mode == 1,
                        onTap = { onSampleTap(s) },
                        onLongPress = { onSampleLongPress(s) },
                        onDragStart = { onSampleDragStart(s) },
                        onDragMove = { off -> onSampleDragMove(s, off) },
                        onDragEnd = { screenPos -> onSampleDragEnd(s, screenPos) },
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
    pulsePhase: Float,
    widthPx: Float, heightPx: Float,
    bankScreenOrigin: Offset,
    muted: Boolean, oneShot: Boolean,
    onTap: () -> Unit, onLongPress: () -> Unit,
    onDragStart: () -> Unit,
    onDragMove: (Offset) -> Unit,
    onDragEnd: (Offset) -> Unit,
    enabled: Boolean,
) {
    val visualState = when {
        s.recording -> SampleVisualState.Recording
        muted       -> SampleVisualState.Muted
        oneShot && !s.playing -> SampleVisualState.Idle
        s.playing && !muted   -> SampleVisualState.Playing
        else -> SampleVisualState.Idle
    }
    val density = LocalDensity.current
    val halfWidthPx = with(density) { 25.dp.toPx() }
    val halfHeightPx = with(density) { 19.dp.toPx() }
    val xPx = (s.positionX * widthPx - halfWidthPx).toInt()
    val yPx = (s.positionY * heightPx - halfHeightPx).toInt()

    var dragDelta by remember { mutableStateOf(Offset.Zero) }

    Box(
        Modifier
            .offset { IntOffset(xPx + dragDelta.x.toInt(), yPx + dragDelta.y.toInt()) }
            .tapLongPressDrag(
                onTap = onTap,
                onLongPress = onLongPress,
                onDragStart = { dragDelta = Offset.Zero; onDragStart() },
                onDragMove = { delta ->
                    dragDelta = delta
                    onDragMove(delta)
                },
                onDragEnd = { delta ->
                    // Compute absolute screen centre of the box at its dropped position
                    val screenPos = bankScreenOrigin +
                        Offset(xPx + halfWidthPx, yPx + halfHeightPx) + delta
                    dragDelta = Offset.Zero
                    onDragEnd(screenPos)
                },
                enabled = enabled,
            )
    ) {
        SampleBox(state = visualState, color = color, bpm = bpm, pulsePhase = pulsePhase)
    }
}
