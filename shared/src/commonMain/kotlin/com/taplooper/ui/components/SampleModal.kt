package com.taplooper.ui.components

import androidx.compose.foundation.background
import androidx.compose.foundation.clickable
import androidx.compose.foundation.layout.*
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.material3.*
import androidx.compose.runtime.*
import androidx.compose.ui.Alignment
import androidx.compose.ui.draw.clip
import androidx.compose.ui.Modifier
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.text.font.FontFamily
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import androidx.compose.ui.window.Dialog

@Composable
fun SampleModal(
    label: String,
    volume: Float,
    reverse: Boolean,
    speed: Int,
    color: Color,
    startOffsetFrames: Int,
    repeat: Boolean,
    onVolumeChange: (Float) -> Unit,
    onReverseChange: (Boolean) -> Unit,
    onSpeedChange: (Int) -> Unit,
    onStartOffsetChange: (Int) -> Unit,
    onRepeatChange: (Boolean) -> Unit,
    onDelete: () -> Unit,
    onDismiss: () -> Unit,
) {
    Dialog(onDismissRequest = onDismiss) {
        var advancedVisible by remember { mutableStateOf(false) }
        Column(
            Modifier
                .background(Color(0xFF0A0A0A), RoundedCornerShape(12.dp))
                .padding(16.dp)
                .fillMaxWidth(),
            verticalArrangement = Arrangement.spacedBy(12.dp)
        ) {
            Text(label, color = color, fontFamily = FontFamily.Monospace, fontSize = 12.sp)
            Text("Volume ${(volume * 100).toInt()}", color = Color.LightGray, fontFamily = FontFamily.Monospace, fontSize = 11.sp)
            Slider(value = volume, onValueChange = onVolumeChange, valueRange = 0f..1f)
            Row(
                verticalAlignment = Alignment.CenterVertically,
                horizontalArrangement = Arrangement.spacedBy(16.dp),
            ) {
                Row(verticalAlignment = Alignment.CenterVertically) {
                    Text("Reverse ", color = Color.LightGray, fontFamily = FontFamily.Monospace, fontSize = 11.sp)
                    Switch(checked = reverse, onCheckedChange = onReverseChange)
                }
                Row(verticalAlignment = Alignment.CenterVertically) {
                    Text("Repeat ", color = Color.LightGray, fontFamily = FontFamily.Monospace, fontSize = 11.sp)
                    Switch(checked = repeat, onCheckedChange = onRepeatChange)
                }
            }
            Row(horizontalArrangement = Arrangement.spacedBy(8.dp)) {
                listOf(0 to "½", 1 to "1", 2 to "2").forEach { (value_, label_) ->
                    Box(
                        Modifier
                            .background(if (speed == value_) color else Color(0xFF222222), RoundedCornerShape(4.dp))
                            .clickable { onSpeedChange(value_) }
                            .padding(horizontal = 16.dp, vertical = 6.dp)
                    ) { Text(label_, color = if (speed == value_) Color.Black else Color.White) }
                }
            }
            Spacer(Modifier.height(4.dp))
            Box(
                Modifier
                    .clickable { advancedVisible = !advancedVisible }
                    .padding(vertical = 2.dp)
            ) {
                Text(
                    text = if (advancedVisible) "ADVANCED ▼" else "ADVANCED ▶",
                    color = Color.LightGray.copy(alpha = 0.6f),
                    fontFamily = FontFamily.Monospace,
                    fontSize = 11.sp,
                )
            }
            if (advancedVisible) {
                val startOffsetMs = startOffsetFrames * 1000 / 48000
                Text("Start shift  ${startOffsetMs}ms", color = Color.LightGray, fontFamily = FontFamily.Monospace, fontSize = 11.sp)
                Slider(
                    value = startOffsetFrames.toFloat(),
                    onValueChange = { onStartOffsetChange(it.toInt()) },
                    valueRange = -24000f..24000f,    // ±500ms at 48 kHz
                )
            }
            Spacer(Modifier.height(8.dp))
            Button(
                onClick = { onDelete(); onDismiss() },
                colors = ButtonDefaults.buttonColors(containerColor = Color(0xFFFF4D00)),
                modifier = Modifier.fillMaxWidth()
            ) { Text("DELETE") }
        }
    }
}
