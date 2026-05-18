package com.taplooper.ui.components

import androidx.compose.animation.core.*
import androidx.compose.foundation.background
import androidx.compose.foundation.border
import androidx.compose.foundation.layout.*
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.runtime.*
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.scale
import androidx.compose.ui.draw.shadow
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.unit.dp
import com.taplooper.ui.theme.LocalTLTheme

enum class SampleVisualState { Idle, Playing, Recording, Muted }

@Composable
fun SampleBox(state: SampleVisualState, color: Color, bpm: Float, modifier: Modifier = Modifier) {
    val theme = LocalTLTheme.current
    val pulseHz = if (bpm > 0) bpm / 60f else 1f
    val durationMs = ((1000f / pulseHz).coerceAtLeast(150f)).toInt()
    val transition = rememberInfiniteTransition(label = "pulse")
    val scale by transition.animateFloat(
        initialValue = 1f, targetValue = theme.pulseScale,
        animationSpec = infiniteRepeatable(tween(durationMs / 2, easing = LinearEasing), RepeatMode.Reverse),
        label = "scale"
    )

    val (bg, border, alpha) = when (state) {
        SampleVisualState.Idle      -> Triple(color.copy(alpha = 0.15f), color, 1f)
        SampleVisualState.Playing   -> Triple(color, color, 1f)
        SampleVisualState.Recording -> Triple(Color.Transparent, Color(0xFFFF4D00), 1f)
        SampleVisualState.Muted     -> Triple(Color.Transparent, color, 0.4f)
    }

    Box(
        modifier
            .size(width = 50.dp, height = 38.dp)
            .scale(if (state == SampleVisualState.Playing || state == SampleVisualState.Recording) scale else 1f)
            .shadow(
                elevation = if (state == SampleVisualState.Playing) theme.pulseGlowDp else 0.dp,
                shape = RoundedCornerShape(4.dp),
                ambientColor = color, spotColor = color
            )
            .border(width = 1.5.dp, color = border.copy(alpha = alpha), shape = RoundedCornerShape(4.dp))
            .background(bg.copy(alpha = alpha), RoundedCornerShape(4.dp))
    )
}
