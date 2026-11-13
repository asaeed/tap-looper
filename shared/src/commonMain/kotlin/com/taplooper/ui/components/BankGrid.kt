package com.taplooper.ui.components

import androidx.compose.foundation.layout.*
import androidx.compose.runtime.*
import androidx.compose.ui.Modifier
import androidx.compose.ui.geometry.Offset
import androidx.compose.ui.geometry.Rect
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.unit.dp
import androidx.compose.ui.zIndex
import com.taplooper.bridge.BankStateKt
import com.taplooper.bridge.SampleStateKt

data class BankCallbacks(
    val onTapEmpty: (Float, Float) -> Unit,
    val onAction: () -> Unit,
    val onToggleMode: () -> Unit,
    val onToggleSolo: () -> Unit,
    val onSampleTap: (SampleStateKt) -> Unit,
    val onSampleLongPress: (SampleStateKt) -> Unit,
    val onSwipe: (Int) -> Unit,   // horizontal swipe on empty bank space: ±1 page direction
    val onSampleDragEnd: (SampleStateKt, Offset) -> Unit,
    val onBodyRect: (Rect) -> Unit,   // bank body screen rect, reported on layout
    val onVolumeChange: (Float) -> Unit,
)

@Composable
fun BankGrid(
    page: Int,
    banks: List<BankStateKt>,
    samples: List<SampleStateKt>,
    bpm: Float,
    recording: Boolean,
    focusedBank: Int,
    bankColors: List<Color>,
    pulsePhase: Float,
    onBankCallbacks: (bankId: Int) -> BankCallbacks,
) {
    val start = page * 4
    // While a sample is dragged, raise its cell and row above the siblings —
    // otherwise banks composed later paint over the dragged box.
    var draggingBank by remember { mutableStateOf<Int?>(null) }
    Column(Modifier.fillMaxSize().padding(6.dp)) {
        Row(
            Modifier.weight(1f).fillMaxWidth()
                .zIndex(if (draggingBank == start || draggingBank == start + 1) 1f else 0f),
            horizontalArrangement = Arrangement.spacedBy(6.dp),
        ) {
            for (i in 0..1) {
                val bankId = start + i
                BankCell(banks, samples, bankId, bankColors, bpm, recording, focusedBank,
                         pulsePhase, onBankCallbacks,
                         onDragActive = { active -> draggingBank = if (active) bankId else null },
                         Modifier.weight(1f).fillMaxHeight()
                             .zIndex(if (draggingBank == bankId) 1f else 0f))
            }
        }
        Spacer(Modifier.height(6.dp))
        Row(
            Modifier.weight(1f).fillMaxWidth()
                .zIndex(if (draggingBank == start + 2 || draggingBank == start + 3) 1f else 0f),
            horizontalArrangement = Arrangement.spacedBy(6.dp),
        ) {
            for (i in 2..3) {
                val bankId = start + i
                BankCell(banks, samples, bankId, bankColors, bpm, recording, focusedBank,
                         pulsePhase, onBankCallbacks,
                         onDragActive = { active -> draggingBank = if (active) bankId else null },
                         Modifier.weight(1f).fillMaxHeight()
                             .zIndex(if (draggingBank == bankId) 1f else 0f))
            }
        }
    }
}

@Composable
private fun BankCell(
    banks: List<BankStateKt>,
    samples: List<SampleStateKt>,
    bankId: Int,
    bankColors: List<Color>,
    bpm: Float,
    recording: Boolean,
    focusedBank: Int,
    pulsePhase: Float,
    onBankCallbacks: (Int) -> BankCallbacks,
    onDragActive: (Boolean) -> Unit,
    modifier: Modifier,
) {
    Box(modifier) {
        val bank = banks[bankId]
        val cb = onBankCallbacks(bankId)
        Bank(
            bankState = bank,
            samples = samples.filter { it.bank == bankId },
            color = bankColors[bankId],
            recording = recording,
            bpm = bpm,
            pulsePhase = pulsePhase,
            isFocused = focusedBank == bankId,
            onTapEmpty = cb.onTapEmpty,
            onAction = cb.onAction,
            onToggleMode = cb.onToggleMode,
            onToggleSolo = cb.onToggleSolo,
            onSampleTap = cb.onSampleTap,
            onSampleLongPress = cb.onSampleLongPress,
            onSwipe = cb.onSwipe,
            onSampleDragEnd = cb.onSampleDragEnd,
            onDragActive = onDragActive,
            onBodyRect = cb.onBodyRect,
            onVolumeChange = cb.onVolumeChange,
        )
    }
}
