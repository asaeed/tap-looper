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
    onCapture: () -> Unit,
    onPageTap: (Int) -> Unit,
) {
    val theme = LocalTLTheme.current
    Row(
        modifier = Modifier
            .fillMaxWidth()
            .height(32.dp)
            .padding(horizontal = 8.dp),
        verticalAlignment = Alignment.CenterVertically,
        horizontalArrangement = Arrangement.SpaceBetween,
    ) {
        Text(
            text = if (recording) "● ${"%.1f".format(recordingDurationMs / 1000f)}s" else "",
            color = theme.recording,
            fontFamily = FontFamily.Monospace,
            fontSize = 11.sp,
        )
        Box(
            Modifier
                .clip(RoundedCornerShape(4.dp))
                .background(Color(0xFF2A0020))
                .clickable { onCapture() }
                .padding(horizontal = 8.dp, vertical = 4.dp)
        ) {
            Text("⎘ CAPTURE", color = theme.bankColors[1], fontFamily = FontFamily.Monospace, fontSize = 11.sp)
        }
        Row {
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
