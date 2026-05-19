package com.taplooper.ui.gestures

import androidx.compose.foundation.gestures.awaitEachGesture
import androidx.compose.foundation.gestures.awaitFirstDown
import androidx.compose.ui.Modifier
import androidx.compose.ui.composed
import androidx.compose.ui.geometry.Offset
import androidx.compose.ui.input.pointer.PointerEventPass
import androidx.compose.ui.input.pointer.pointerInput
import kotlinx.coroutines.withTimeoutOrNull
import kotlin.math.abs

fun Modifier.tapLongPressDrag(
    onTap: () -> Unit,
    onLongPress: () -> Unit,
    onDragStart: () -> Unit,
    onDragMove: (Offset) -> Unit,
    onDragEnd: (Offset) -> Unit,
    enabled: Boolean,
    movementThresholdPx: Float = 12f,
    longPressDelayMs: Long = 500L,
): Modifier = composed {
    pointerInput(enabled) {
        if (!enabled) return@pointerInput
        awaitEachGesture {
            val down = awaitFirstDown(requireUnconsumed = false)
            down.consume()
            val downTime = System.currentTimeMillis()
            var totalDelta = Offset.Zero
            var dragging = false
            var longPressed = false

            while (true) {
                val elapsed = System.currentTimeMillis() - downTime
                val remaining = (longPressDelayMs - elapsed).coerceAtLeast(0L)

                val event = if (!longPressed && !dragging && remaining > 0) {
                    withTimeoutOrNull(remaining) { awaitPointerEvent(PointerEventPass.Main) }
                } else {
                    awaitPointerEvent(PointerEventPass.Main)
                }

                if (event == null) {
                    // Timed out waiting — fire long-press
                    longPressed = true
                    onLongPress()
                    // Drain remaining events until finger up, consuming them
                    while (true) {
                        val e = awaitPointerEvent(PointerEventPass.Main)
                        val c = e.changes.firstOrNull { it.id == down.id } ?: break
                        c.consume()
                        if (!c.pressed) break
                    }
                    break
                }

                val change = event.changes.firstOrNull { it.id == down.id } ?: break
                change.consume()
                totalDelta += change.position - down.position
                val movedFar = abs(totalDelta.x) > movementThresholdPx || abs(totalDelta.y) > movementThresholdPx

                if (!longPressed && !dragging && movedFar) {
                    dragging = true
                    onDragStart()
                }
                if (dragging) onDragMove(totalDelta)

                if (!change.pressed) {
                    when {
                        dragging    -> onDragEnd(totalDelta)
                        longPressed -> { /* already handled */ }
                        else        -> onTap()
                    }
                    break
                }
            }
        }
    }
}
