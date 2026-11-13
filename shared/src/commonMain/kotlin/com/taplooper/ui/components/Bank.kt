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
import androidx.compose.ui.geometry.Rect
import androidx.compose.ui.geometry.Size
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.input.pointer.pointerInput
import androidx.compose.ui.layout.onGloballyPositioned
import androidx.compose.ui.layout.positionInRoot
import androidx.compose.ui.platform.LocalDensity
import androidx.compose.ui.unit.IntOffset
import androidx.compose.ui.unit.dp
import androidx.compose.ui.zIndex
import com.taplooper.bridge.BankStateKt
import com.taplooper.bridge.SampleStateKt
import com.taplooper.ui.gestures.tapLongPressDrag
import kotlin.math.abs
import kotlinx.coroutines.delay

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
    onToggleSolo: () -> Unit,
    onSampleTap: (SampleStateKt) -> Unit,
    onSampleLongPress: (SampleStateKt) -> Unit,
    onSwipe: (Int) -> Unit,
    onSampleDragEnd: (SampleStateKt, Offset) -> Unit,
    onDragActive: (Boolean) -> Unit,
    onBodyRect: (Rect) -> Unit,
    onVolumeChange: (Float) -> Unit,
    pulsePhase: Float,
    isFocused: Boolean,
) {
    // Screen origin of the bank BODY (the area boxes are placed in) — not the
    // whole bank: using the bank origin made every drop land a header-height off.
    var bodyScreenOrigin by remember { mutableStateOf(Offset.Zero) }
    val recordingNow = rememberUpdatedState(recording)

    BoxWithConstraints(
        Modifier
            .fillMaxSize()
            .border(if (isFocused) 1.dp else 0.dp, color.copy(alpha = 0.4f), RoundedCornerShape(8.dp))
            .background(Color(0xFF0A0A0A), RoundedCornerShape(8.dp))
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
                soloed = bankState.soloed,
                onToggleMode = onToggleMode,
                onAction = onAction,
                onToggleSolo = onToggleSolo,
            )
            Box(
                Modifier
                    .weight(1f)
                    .fillMaxWidth()
                    .onGloballyPositioned { coords ->
                        bodyScreenOrigin = coords.positionInRoot()
                        // Report with the BANK's dimensions (not the body's): sample
                        // positions are normalized against the bank constraints, so
                        // the drop inverse must use the same basis.
                        onBodyRect(Rect(bodyScreenOrigin, Size(widthPx, heightPx)))
                    }
                    .pointerInput(Unit) {
                        awaitEachGesture {
                            val down = awaitFirstDown(requireUnconsumed = true)
                            if (recordingNow.value) return@awaitEachGesture
                            // Clamp so box never straddles bank edge (Issue C)
                            val halfWPx = with(density) { (SampleBoxWidth / 2).toPx() }
                            val halfHPx = with(density) { (SampleBoxHeight / 2).toPx() }
                            val minX = halfWPx / widthPx
                            val maxX = 1f - halfWPx / widthPx
                            val minY = halfHPx / heightPx
                            val maxY = 1f - halfHPx / heightPx
                            val xNorm = (down.position.x / widthPx).coerceIn(minX, maxX)
                            val yNorm = (down.position.y / heightPx).coerceIn(minY, maxY)
                            onTapEmpty(xNorm, yNorm)
                            // Recording started on press-down. If the gesture turns out to
                            // be a horizontal page swipe, discard the recording and page.
                            while (true) {
                                val event = awaitPointerEvent()
                                val ch = event.changes.firstOrNull { it.id == down.id } ?: break
                                val delta = ch.position - down.position
                                if (abs(delta.x) > 30f && abs(delta.x) > abs(delta.y)) {
                                    ch.consume()
                                    onSwipe(if (delta.x < 0) 1 else -1)
                                    break
                                }
                                if (!ch.pressed) break
                            }
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
                        bodyScreenOrigin = bodyScreenOrigin,
                        muted = bankState.muted,
                        oneShot = bankState.mode == 1,
                        onTap = { onSampleTap(s) },
                        onLongPress = { onSampleLongPress(s) },
                        onDragEnd = { screenPos -> onSampleDragEnd(s, screenPos) },
                        onDragActive = onDragActive,
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
    bodyScreenOrigin: Offset,
    muted: Boolean, oneShot: Boolean,
    onTap: () -> Unit, onLongPress: () -> Unit,
    onDragEnd: (Offset) -> Unit,
    onDragActive: (Boolean) -> Unit,
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
    val halfWidthPx = with(density) { (SampleBoxWidth / 2).toPx() }
    val halfHeightPx = with(density) { (SampleBoxHeight / 2).toPx() }
    val xPx = (s.positionX * widthPx - halfWidthPx).toInt()
    val yPx = (s.positionY * heightPx - halfHeightPx).toInt()

    var dragDelta by remember { mutableStateOf(Offset.Zero) }
    var dragging by remember { mutableStateOf(false) }
    var pendingDrop by remember { mutableStateOf(false) }

    // After a drop, hold the drag offset until the engine round-trips the new
    // position — resetting immediately flashed the box back at its old spot
    // for one poll cycle. The timeout covers a move the engine quantized to
    // the identical position (keys unchanged → no reset from the first effect).
    LaunchedEffect(s.positionX, s.positionY, s.bank) {
        dragDelta = Offset.Zero
        pendingDrop = false
    }
    LaunchedEffect(pendingDrop) {
        if (pendingDrop) {
            delay(250)
            dragDelta = Offset.Zero
            pendingDrop = false
        }
    }

    Box(
        Modifier
            .offset { IntOffset(xPx + dragDelta.x.toInt(), yPx + dragDelta.y.toInt()) }
            // Draw above sibling boxes while dragging (cross-bank layering is
            // handled by BankGrid raising the whole cell/row).
            .zIndex(if (dragging || pendingDrop) 1f else 0f)
            .tapLongPressDrag(
                onTap = onTap,
                onLongPress = onLongPress,
                onDragStart = { dragDelta = Offset.Zero; dragging = true; onDragActive(true) },
                onDragMove = { delta -> dragDelta = delta },
                onDragEnd = { delta ->
                    // Compute absolute screen centre of the box at its dropped position
                    val screenPos = bodyScreenOrigin +
                        Offset(xPx + halfWidthPx, yPx + halfHeightPx) + delta
                    dragging = false
                    pendingDrop = true
                    onDragActive(false)
                    onDragEnd(screenPos)
                },
                enabled = enabled,
            )
    ) {
        SampleBox(
            state = visualState, color = color, bpm = bpm,
            pulsePhase = pulsePhase, envelope = s.envelope,
        )
    }
}
