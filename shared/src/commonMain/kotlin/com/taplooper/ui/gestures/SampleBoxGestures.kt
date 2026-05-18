package com.taplooper.ui.gestures

import androidx.compose.foundation.gestures.awaitEachGesture
import androidx.compose.foundation.gestures.awaitFirstDown
import androidx.compose.ui.Modifier
import androidx.compose.ui.composed
import androidx.compose.ui.geometry.Offset
import androidx.compose.ui.input.pointer.PointerEventPass
import androidx.compose.ui.input.pointer.PointerInputChange
import androidx.compose.ui.input.pointer.pointerInput
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
            val downTime = System.currentTimeMillis()
            var resolved = false
            var totalDelta = Offset.Zero
            var dragging = false

            while (true) {
                val event = awaitPointerEvent(PointerEventPass.Main)
                val change: PointerInputChange = event.changes.firstOrNull { it.id == down.id } ?: break
                totalDelta += change.position - down.position
                val movedFar = abs(totalDelta.x) > movementThresholdPx || abs(totalDelta.y) > movementThresholdPx
                if (!resolved && movedFar) {
                    resolved = true
                    dragging = true
                    onDragStart()
                }
                if (dragging) onDragMove(change.position)
                if (!change.pressed) {
                    val elapsed = System.currentTimeMillis() - downTime
                    when {
                        dragging         -> onDragEnd(change.position)
                        elapsed >= longPressDelayMs -> onLongPress()
                        else             -> onTap()
                    }
                    break
                }
                if (!resolved && System.currentTimeMillis() - downTime >= longPressDelayMs && !movedFar) {
                    resolved = true
                    onLongPress()
                    while (true) {
                        val e2 = awaitPointerEvent(PointerEventPass.Main)
                        val c2 = e2.changes.firstOrNull { it.id == down.id } ?: break
                        if (!c2.pressed) break
                    }
                    break
                }
            }
        }
    }
}
