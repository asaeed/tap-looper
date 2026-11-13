package com.taplooper.ui.components

import androidx.compose.animation.core.*
import androidx.compose.foundation.Canvas
import androidx.compose.foundation.background
import androidx.compose.foundation.border
import androidx.compose.foundation.layout.*
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.runtime.*
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.scale
import androidx.compose.ui.draw.shadow
import androidx.compose.ui.geometry.Offset
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.unit.dp
import com.taplooper.ui.theme.LocalTLTheme
import kotlin.math.PI
import kotlin.math.sin

enum class SampleVisualState { Idle, Playing, Recording, Muted }

// Single source of truth for the box footprint — Bank.kt derives its
// half-box clamp margins from these.
val SampleBoxWidth = 50.dp
val SampleBoxHeight = 38.dp

@Composable
fun SampleBox(
    state: SampleVisualState,
    color: Color,
    bpm: Float,
    pulsePhase: Float = 0f,            // 0..1 phase shared by all playing boxes
    envelope: List<Float> = emptyList(),   // waveform thumbnail (peak per bin)
    modifier: Modifier = Modifier
) {
    val theme = LocalTLTheme.current

    // Recording boxes pulse independently at ~3 Hz for eye-catching effect.
    val recordingTransition = rememberInfiniteTransition(label = "rec-pulse")
    val recordingPhase by recordingTransition.animateFloat(
        initialValue = 0f, targetValue = 1f,
        animationSpec = infiniteRepeatable(tween(300, easing = LinearEasing), RepeatMode.Restart),
        label = "rec-phase"
    )

    val phase = when (state) {
        SampleVisualState.Recording -> recordingPhase
        SampleVisualState.Playing   -> pulsePhase
        else                        -> 0f
    }
    val pulseAmount = 0.5f + 0.5f * sin(phase * 2f * PI.toFloat())
    // pulseAmount in [0..1]
    val scaleTarget = 1f + (theme.pulseScale - 1f) * pulseAmount

    val (bg, border, alpha) = when (state) {
        SampleVisualState.Idle      -> Triple(color.copy(alpha = 0.08f), color.copy(alpha = 0.5f), 1f)
        SampleVisualState.Playing   -> Triple(color, color, 1f)
        SampleVisualState.Recording -> Triple(Color.Transparent, Color(0xFFFF4D00), 1f)
        SampleVisualState.Muted     -> Triple(Color.Transparent, color.copy(alpha = 0.3f), 0.4f)
    }

    val elevation = when (state) {
        SampleVisualState.Playing   -> theme.pulseGlowDp * pulseAmount
        SampleVisualState.Recording -> 16.dp * pulseAmount
        else                        -> 0.dp
    }

    Box(
        modifier
            .size(width = SampleBoxWidth, height = SampleBoxHeight)
            .scale(if (state == SampleVisualState.Playing || state == SampleVisualState.Recording) scaleTarget else 1f)
            .shadow(
                elevation = elevation,
                shape = RoundedCornerShape(4.dp),
                ambientColor = color, spotColor = color
            )
            .border(width = 1.5.dp, color = border.copy(alpha = alpha), shape = RoundedCornerShape(4.dp))
            .background(bg.copy(alpha = alpha), RoundedCornerShape(4.dp))
    ) {
        val peak = envelope.maxOrNull() ?: 0f
        if (peak > 0f) {
            // Waveform thumbnail: normalized peak-per-bin bars mirrored around centre.
            val waveColor = when (state) {
                SampleVisualState.Playing -> Color.Black.copy(alpha = 0.35f)
                else                      -> color.copy(alpha = 0.45f * alpha)
            }
            Canvas(Modifier.fillMaxSize().padding(horizontal = 4.dp, vertical = 3.dp)) {
                val bw = size.width / envelope.size
                val midY = size.height / 2f
                val maxAmp = size.height * 0.48f
                envelope.forEachIndexed { i, v ->
                    val h = (v / peak).coerceIn(0.04f, 1f) * maxAmp
                    val x = i * bw + bw / 2f
                    drawLine(waveColor, Offset(x, midY - h), Offset(x, midY + h), strokeWidth = bw * 0.6f)
                }
            }
        }
    }
}
