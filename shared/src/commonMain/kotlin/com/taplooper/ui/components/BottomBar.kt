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
    extraLatencyFrames: Int,
    metronome: Boolean,
    onChangeBeats: (Int) -> Unit,
    onChangeMasterBarFrames: (Int) -> Unit,
    onChangeExtraLatency: (Int) -> Unit,
    onToggleMetronome: () -> Unit,
) {
    val theme = LocalTLTheme.current
    // Two rows: tempo controls on top, alignment/metronome tools below —
    // one row overflows the 360dp default window width.
    Column(Modifier.fillMaxWidth().padding(vertical = 6.dp, horizontal = 8.dp)) {
        Row(
            Modifier.fillMaxWidth(),
            verticalAlignment = Alignment.CenterVertically,
            horizontalArrangement = Arrangement.Center
        ) {
            Text("BPM ${"%.1f".format(bpm)}", color = theme.text, fontFamily = FontFamily.Monospace, fontSize = 12.sp)
            Spacer(Modifier.width(8.dp))

            // ÷2 / ×2 (coarse beats_per_bar swap)
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

            // ±0.1 BPM fine-tune — only once a master bar exists.
            if (masterBarFrames > 0) {
                Spacer(Modifier.width(8.dp))
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

        if (masterBarFrames > 0) {
            Spacer(Modifier.height(4.dp))
            Row(
                Modifier.fillMaxWidth(),
                verticalAlignment = Alignment.CenterVertically,
                horizontalArrangement = Arrangement.Center
            ) {
                // ±2ms latency nudge — lets the user align subsequent recordings by ear.
                val latMs = extraLatencyFrames * 1000 / kSampleRate
                Text(
                    "LAT ${if (latMs >= 0) "+" else ""}${latMs}ms",
                    color = theme.text,
                    fontFamily = FontFamily.Monospace,
                    fontSize = 11.sp
                )
                Spacer(Modifier.width(4.dp))
                listOf(-96 to "−2", 96 to "+2").forEach { (delta, label) ->
                    Box(
                        Modifier
                            .clip(RoundedCornerShape(4.dp))
                            .background(Color(0xFF111111))
                            .clickable { onChangeExtraLatency(extraLatencyFrames + delta) }
                            .padding(horizontal = 6.dp, vertical = 2.dp)
                    ) { Text(label, color = theme.bankColors[3], fontFamily = FontFamily.Monospace, fontSize = 10.sp) }
                    Spacer(Modifier.width(4.dp))
                }

                // Metronome toggle — clicks on every beat of the master bar.
                Spacer(Modifier.width(8.dp))
                Box(
                    Modifier
                        .clip(RoundedCornerShape(4.dp))
                        .background(if (metronome) Color(0x33FFEA00) else Color(0xFF111111))
                        .clickable { onToggleMetronome() }
                        .padding(horizontal = 6.dp, vertical = 2.dp)
                ) {
                    Text(
                        "MET",
                        color = if (metronome) Color(0xFFFFEA00) else theme.text,
                        fontFamily = FontFamily.Monospace, fontSize = 10.sp
                    )
                }
            }
        }
    }
}
