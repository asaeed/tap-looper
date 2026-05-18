package com.taplooper.ui.components

import androidx.compose.foundation.background
import androidx.compose.foundation.gestures.detectHorizontalDragGestures
import androidx.compose.foundation.gestures.detectTapGestures
import androidx.compose.foundation.layout.*
import androidx.compose.runtime.*
import androidx.compose.ui.Modifier
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.input.pointer.pointerInput
import androidx.compose.ui.layout.onGloballyPositioned
import androidx.compose.ui.unit.dp

@Composable
fun VolumeStrip(volume: Float, color: Color, onVolumeChange: (Float) -> Unit) {
    var width by remember { mutableFloatStateOf(1f) }
    Box(
        Modifier
            .fillMaxWidth()
            .height(12.dp)
            .background(Color(0xFF0A0A0A))
            .onGloballyPositioned { width = it.size.width.toFloat() }
            .pointerInput(Unit) {
                detectTapGestures { offset -> onVolumeChange((offset.x / width).coerceIn(0f, 1f)) }
            }
            .pointerInput(Unit) {
                detectHorizontalDragGestures { change, _ ->
                    onVolumeChange((change.position.x / width).coerceIn(0f, 1f))
                }
            }
    ) {
        Box(Modifier.fillMaxHeight().fillMaxWidth(volume).background(color))
    }
}
