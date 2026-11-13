package com.taplooper.ui.components

import androidx.compose.foundation.ExperimentalFoundationApi
import androidx.compose.foundation.background
import androidx.compose.foundation.clickable
import androidx.compose.foundation.combinedClickable
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

@OptIn(ExperimentalFoundationApi::class)
@Composable
fun TopBar(
    recording: Boolean,
    recordingDurationMs: Int,
    currentPage: Int,
    masterPlaying: Boolean,
    inputPeak: Float,
    showSaved: Boolean,
    onCapture: () -> Unit,
    onSave: () -> Unit,
    onReset: () -> Unit,
    onTogglePlay: () -> Unit,
    onPageTap: (Int) -> Unit,
) {
    val theme = LocalTLTheme.current
    Row(
        modifier = Modifier
            .fillMaxWidth()
            .height(32.dp)
            .padding(horizontal = 6.dp),
        verticalAlignment = Alignment.CenterVertically,
        horizontalArrangement = Arrangement.spacedBy(6.dp),
    ) {
        // Status slot, fixed width so it doesn't shift layout:
        // mic level dot + REC timer / SAVED confirmation
        Row(
            verticalAlignment = Alignment.CenterVertically,
            modifier = Modifier.widthIn(min = 56.dp),
        ) {
            val level = inputPeak.coerceIn(0f, 1f)
            Box(
                Modifier
                    .size(8.dp)
                    .clip(CircleShape)
                    .background(
                        if (level > 0.98f) theme.recording   // clipping
                        else Color(0xFF00FF66).copy(alpha = 0.15f + 0.85f * level)
                    )
            )
            Spacer(Modifier.width(4.dp))
            Text(
                text = when {
                    recording -> "● ${"%.1f".format(recordingDurationMs / 1000f)}s"
                    showSaved -> "SAVED"
                    else -> ""
                },
                color = if (recording) theme.recording else Color(0xFF00FF66),
                fontFamily = FontFamily.Monospace,
                fontSize = 11.sp,
            )
        }
        // CAPTURE button
        Box(
            Modifier
                .clip(RoundedCornerShape(4.dp))
                .background(Color(0xFF2A0020))
                .clickable { onCapture() }
                .padding(horizontal = 6.dp, vertical = 4.dp)
        ) {
            Text("⎘ CAPTURE", color = theme.bankColors[1], fontFamily = FontFamily.Monospace, fontSize = 11.sp)
        }
        // SAVE (export WAVs + session.json)
        Box(
            Modifier
                .clip(RoundedCornerShape(4.dp))
                .background(Color(0xFF002A20))
                .clickable { onSave() }
                .padding(horizontal = 6.dp, vertical = 4.dp)
        ) {
            Text("SAVE", color = theme.bankColors[0], fontFamily = FontFamily.Monospace, fontSize = 11.sp)
        }
        Spacer(Modifier.weight(1f))
        // PLAY / STOP toggle
        Box(
            Modifier
                .clip(RoundedCornerShape(4.dp))
                .background(if (masterPlaying) Color(0xFF003322) else Color(0xFF220000))
                .clickable { onTogglePlay() }
                .padding(horizontal = 6.dp, vertical = 4.dp)
        ) {
            Text(
                if (masterPlaying) "■ STOP" else "▶ PLAY",
                color = if (masterPlaying) Color(0xFF00FF66) else Color(0xFF00FFD5),
                fontFamily = FontFamily.Monospace,
                fontSize = 11.sp,
            )
        }
        // RESET + page pips
        Row(verticalAlignment = Alignment.CenterVertically) {
            // Long-press only: an accidental tap must not wipe the session.
            Box(
                Modifier
                    .clip(RoundedCornerShape(4.dp))
                    .background(Color(0xFF2A0000))
                    .combinedClickable(onClick = {}, onLongClick = onReset)
                    .padding(horizontal = 6.dp, vertical = 4.dp)
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
