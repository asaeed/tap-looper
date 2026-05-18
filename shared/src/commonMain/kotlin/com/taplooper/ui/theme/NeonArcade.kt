package com.taplooper.ui.theme

import androidx.compose.runtime.staticCompositionLocalOf
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.unit.Dp
import androidx.compose.ui.unit.dp

data class TLTheme(
    val background: Color = Color(0xFF000000),
    val text:       Color = Color(0xFFAAAAAA),
    val recording:  Color = Color(0xFFFF4D00),
    val bankColors: List<Color> = listOf(
        Color(0xFF00FFD5), // cyan
        Color(0xFFFF00AA), // magenta
        Color(0xFF9D00FF), // violet
        Color(0xFFFF4D00), // red-orange
        Color(0xFF00FF66), // green
        Color(0xFFFFEA00), // yellow
        Color(0xFF00B0FF), // blue
        Color(0xFFFF005C), // pink
    ),
    val pulseScale: Float = 1.04f,
    val pulseGlowDp: Dp = 12.dp,
)

val LocalTLTheme = staticCompositionLocalOf { TLTheme() }
