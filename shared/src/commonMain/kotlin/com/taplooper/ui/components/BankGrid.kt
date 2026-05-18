package com.taplooper.ui.components

import androidx.compose.foundation.layout.*
import androidx.compose.runtime.Composable
import androidx.compose.ui.Modifier
import androidx.compose.ui.geometry.Offset
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.unit.dp
import com.taplooper.bridge.BankStateKt
import com.taplooper.bridge.SampleStateKt

data class BankCallbacks(
    val onTapEmpty: (Float, Float) -> Unit,
    val onAction: () -> Unit,
    val onToggleMode: () -> Unit,
    val onSampleTap: (SampleStateKt) -> Unit,
    val onSampleLongPress: (SampleStateKt) -> Unit,
    val onSampleDragStart: (SampleStateKt) -> Unit,
    val onSampleDragMove: (SampleStateKt, Offset) -> Unit,
    val onSampleDragEnd: (SampleStateKt, Offset) -> Unit,
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
    onBankCallbacks: (bankId: Int) -> BankCallbacks,
) {
    val start = page * 4
    Column(Modifier.fillMaxSize().padding(6.dp)) {
        Row(Modifier.weight(1f).fillMaxWidth(), horizontalArrangement = Arrangement.spacedBy(6.dp)) {
            for (i in 0..1) {
                val bankId = start + i
                BankCell(banks, samples, bankId, bankColors, bpm, recording, focusedBank,
                         onBankCallbacks, Modifier.weight(1f).fillMaxHeight())
            }
        }
        Spacer(Modifier.height(6.dp))
        Row(Modifier.weight(1f).fillMaxWidth(), horizontalArrangement = Arrangement.spacedBy(6.dp)) {
            for (i in 2..3) {
                val bankId = start + i
                BankCell(banks, samples, bankId, bankColors, bpm, recording, focusedBank,
                         onBankCallbacks, Modifier.weight(1f).fillMaxHeight())
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
    onBankCallbacks: (Int) -> BankCallbacks,
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
            isFocused = focusedBank == bankId,
            onTapEmpty = cb.onTapEmpty,
            onAction = cb.onAction,
            onToggleMode = cb.onToggleMode,
            onSampleTap = cb.onSampleTap,
            onSampleLongPress = cb.onSampleLongPress,
            onSampleDragStart = cb.onSampleDragStart,
            onSampleDragMove = cb.onSampleDragMove,
            onSampleDragEnd = cb.onSampleDragEnd,
            onVolumeChange = cb.onVolumeChange,
        )
    }
}
