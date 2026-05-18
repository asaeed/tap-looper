package com.taplooper.ui.components

import androidx.compose.foundation.border
import androidx.compose.foundation.clickable
import androidx.compose.foundation.layout.*
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.text.font.FontFamily
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp

@Composable
fun BankHeader(
    label: String,
    color: Color,
    oneShot: Boolean,
    muted: Boolean,
    onToggleMode: () -> Unit,
    onAction: () -> Unit,
) {
    Row(
        Modifier.fillMaxWidth().padding(horizontal = 6.dp, vertical = 4.dp),
        verticalAlignment = Alignment.CenterVertically,
        horizontalArrangement = Arrangement.SpaceBetween,
    ) {
        Text(label, color = color, fontFamily = FontFamily.Monospace, fontSize = 10.sp)
        Box(
            Modifier
                .border(1.dp, color, RoundedCornerShape(2.dp))
                .clickable { onAction() }
                .padding(horizontal = 6.dp, vertical = 2.dp)
        ) {
            Text(
                text = if (oneShot) "▶ ALL" else if (muted) "MUTED" else "MUTE",
                color = color, fontFamily = FontFamily.Monospace, fontSize = 9.sp
            )
        }
        Box(
            Modifier
                .border(1.dp, color, RoundedCornerShape(2.dp))
                .clickable { onToggleMode() }
                .padding(horizontal = 6.dp, vertical = 2.dp)
        ) {
            Text(if (oneShot) "ONE" else "LOOP", color = color, fontFamily = FontFamily.Monospace, fontSize = 9.sp)
        }
    }
}
