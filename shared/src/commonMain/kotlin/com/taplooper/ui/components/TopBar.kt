package com.taplooper.ui.components

import androidx.compose.foundation.background
import androidx.compose.foundation.clickable
import androidx.compose.foundation.layout.*
import androidx.compose.foundation.shape.CircleShape
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.clip
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.text.font.FontFamily
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import com.taplooper.ui.theme.LocalTLTheme

@Composable
fun TopBar(
    recording: Boolean,
    recordingDurationMs: Int,
    currentPage: Int,
    masterPlaying: Boolean,
    onCapture: () -> Unit,
    onReset: () -> Unit,
    onTogglePlay: () -> Unit,
    onPageTap: (Int) -> Unit,
) {
    val theme = LocalTLTheme.current
    Row(
        modifier = Modifier
            .fillMaxWidth()
            .height(32.dp)
            .padding(horizontal = 8.dp),
        verticalAlignment = Alignment.CenterVertically,
        horizontalArrangement = Arrangement.spacedBy(8.dp),
    ) {
        // REC indicator (fixed width so it doesn't shift layout)
        Text(
            text = if (recording) "● ${"%.1f".format(recordingDurationMs / 1000f)}s" else "",
            color = theme.recording,
            fontFamily = FontFamily.Monospace,
            fontSize = 11.sp,
            modifier = Modifier.widthIn(min = 56.dp),
        )
        // PLAY / STOP toggle
        Box(
            Modifier
                .clip(RoundedCornerShape(4.dp))
                .background(if (masterPlaying) Color(0xFF003322) else Color(0xFF220000))
                .clickable { onTogglePlay() }
                .padding(horizontal = 8.dp, vertical = 4.dp)
        ) {
            Text(
                if (masterPlaying) "■ STOP" else "▶ PLAY",
                color = if (masterPlaying) Color(0xFF00FF66) else Color(0xFF00FFD5),
                fontFamily = FontFamily.Monospace,
                fontSize = 11.sp,
            )
        }
        Spacer(Modifier.weight(1f))
        // CAPTURE button
        Box(
            Modifier
                .clip(RoundedCornerShape(4.dp))
                .background(Color(0xFF2A0020))
                .clickable { onCapture() }
                .padding(horizontal = 8.dp, vertical = 4.dp)
        ) {
            Text("⎘ CAPTURE", color = theme.bankColors[1], fontFamily = FontFamily.Monospace, fontSize = 11.sp)
        }
        // RESET + page pips
        Row(verticalAlignment = Alignment.CenterVertically) {
            Box(
                Modifier
                    .clip(RoundedCornerShape(4.dp))
                    .background(Color(0xFF2A0000))
                    .clickable { onReset() }
                    .padding(horizontal = 8.dp, vertical = 4.dp)
            ) {
                Text("RESET", color = Color(0xFFFF4D00), fontFamily = FontFamily.Monospace, fontSize = 11.sp)
            }
            Spacer(Modifier.width(6.dp))
            repeat(2) { i ->
                Box(
                    Modifier
                        .padding(2.dp)
                        .size(8.dp)
                        .clip(CircleShape)
                        .background(if (i == currentPage) theme.bankColors[0] else Color(0xFF333333))
                        .clickable { onPageTap(i) }
                )
            }
        }
    }
}
