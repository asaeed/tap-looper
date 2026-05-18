package com.taplooper.bridge

data class SampleStateKt(
    val id: Long,
    val bank: Int,
    val volume: Float,
    val reverse: Boolean,
    val speed: Int,        // 0=Half, 1=Normal, 2=Double
    val enabled: Boolean,
    val playing: Boolean,
    val recording: Boolean,
    val positionX: Float,
    val positionY: Float,
    val recordedLengthFrames: Int,
    val recordedOffsetInBar: Int,
)

data class BankStateKt(
    val id: Int,
    val mode: Int,         // 0=Loop, 1=OneShot
    val volume: Float,
    val muted: Boolean,
)

data class EngineStateKt(
    val banks: List<BankStateKt>,
    val samples: List<SampleStateKt>,
    val recording: Boolean,
    val recordingDurationFrames: Int,
    val recordingSampleId: Long,
    val focusedBank: Int,
    val masterBarFrames: Int,
    val beatsPerBar: Int,
    val bpm: Float,
    val version: Long,
)
