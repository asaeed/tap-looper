package com.taplooper.bridge

class EngineBridge {
    private val handle: Long = nativeCreate()

    fun start() = nativeStart(handle)
    fun stop()  = nativeStop(handle)
    fun close() = nativeDestroy(handle)

    fun submit(
        type: Int,
        sampleId: Long = 0,
        bank: Int = 0,
        fx: Float = 0f, fy: Float = 0f, fvalue: Float = 0f,
        ivalue: Int = 0, bvalue: Boolean = false
    ): Boolean = nativeSubmit(handle, type, sampleId, bank, fx, fy, fvalue, ivalue, bvalue)

    fun snapshot(): EngineStateKt {
        val longs = LongArray(9 + 8 * 2 + 64 * 8)
        val floats = FloatArray(1 + 8 + 64 * 4)
        nativeSnapshot(handle, longs, floats)
        val banks = (0 until 8).map { i ->
            val lp = 9 + i * 2
            val fp = 1 + i
            BankStateKt(id = i, mode = longs[lp].toInt(), muted = longs[lp+1] == 1L, volume = floats[fp])
        }
        val count = longs[1].toInt()
        val samples = (0 until count).map { i ->
            val lp = 9 + 8*2 + i * 8
            val fp = 1 + 8 + i * 4
            SampleStateKt(
                id = longs[lp],
                bank = longs[lp+1].toInt(),
                speed = longs[lp+2].toInt(),
                enabled = longs[lp+3] == 1L,
                playing = longs[lp+4] == 1L,
                recording = longs[lp+5] == 1L,
                recordedLengthFrames = longs[lp+6].toInt(),
                recordedOffsetInBar = longs[lp+7].toInt(),
                volume = floats[fp],
                reverse = floats[fp+1] != 0f,
                positionX = floats[fp+2],
                positionY = floats[fp+3],
            )
        }
        return EngineStateKt(
            banks = banks,
            samples = samples,
            recording = longs[4] == 1L,
            recordingDurationFrames = longs[5].toInt(),
            recordingSampleId = longs[6],
            focusedBank = longs[2].toInt(),
            masterBarFrames = longs[3].toInt(),
            beatsPerBar = longs[7].toInt(),
            masterPlaying = longs[8] == 1L,
            bpm = floats[0],
            version = longs[0],
        )
    }

    private external fun nativeCreate(): Long
    private external fun nativeStart(h: Long)
    private external fun nativeStop(h: Long)
    private external fun nativeDestroy(h: Long)
    private external fun nativeSubmit(
        h: Long,
        type: Int, sampleId: Long, bank: Int,
        fx: Float, fy: Float, fvalue: Float,
        ivalue: Int, bvalue: Boolean
    ): Boolean
    private external fun nativeSnapshot(h: Long, longs: LongArray, floats: FloatArray)

    companion object {
        init { System.loadLibrary("tap_looper_bridge") }
    }
}
