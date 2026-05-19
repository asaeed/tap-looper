package com.taplooper.ui.components

import androidx.compose.foundation.background
import androidx.compose.foundation.clickable
import androidx.compose.foundation.layout.*
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

private const val kSampleRate = 48000

@Composable
fun BottomBar(
    bpm: Float,
    beatsPerBar: Int,
    masterBarFrames: Int,
    onChangeBeats: (Int) -> Unit,
    onChangeMasterBarFrames: (Int) -> Unit,
) {
    val theme = LocalTLTheme.current
    Row(
        Modifier.fillMaxWidth().padding(8.dp),
        verticalAlignment = Alignment.CenterVertically,
        horizontalArrangement = Arrangement.Center
    ) {
        Text("BPM ${"%.1f".format(bpm)}", color = theme.text, fontFamily = FontFamily.Monospace, fontSize = 12.sp)
        Spacer(Modifier.width(8.dp))

        // ÷2 / ×2 (existing — coarse beats_per_bar swap)
        listOf(2 to "÷2", 8 to "×2").forEach { (_, label) ->
            Box(
                Modifier
                    .clip(RoundedCornerShape(4.dp))
                    .background(Color(0xFF111111))
                    .clickable {
                        val v = when {
                            label == "÷2" -> when (beatsPerBar) { 8 -> 4; 4 -> 2; else -> 2 }
                            else          -> when (beatsPerBar) { 2 -> 4; 4 -> 8; else -> 8 }
                        }
                        onChangeBeats(v)
                    }
                    .padding(horizontal = 6.dp, vertical = 2.dp)
            ) { Text(label, color = theme.bankColors[0], fontFamily = FontFamily.Monospace, fontSize = 10.sp) }
            Spacer(Modifier.width(4.dp))
        }

        // ±0.1 BPM fine-tune — always visible. Only enabled once a master bar exists.
        if (masterBarFrames > 0) {
            Spacer(Modifier.width(12.dp))
            listOf(-0.1f to "−0.1", 0.1f to "+0.1").forEach { (delta, label) ->
                Box(
                    Modifier
                        .clip(RoundedCornerShape(4.dp))
                        .background(Color(0xFF111111))
                        .clickable {
                            val newBpm = (bpm + delta).coerceAtLeast(20f)
                            val newL = (beatsPerBar * 60f * kSampleRate / newBpm).toInt()
                            onChangeMasterBarFrames(newL)
                        }
                        .padding(horizontal = 6.dp, vertical = 2.dp)
                ) { Text(label, color = theme.bankColors[2], fontFamily = FontFamily.Monospace, fontSize = 10.sp) }
                Spacer(Modifier.width(4.dp))
            }
        }
    }
}
