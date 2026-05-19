package com.taplooper.domain

import com.taplooper.bridge.CommandType as CT
import com.taplooper.bridge.EngineBridge
import com.taplooper.bridge.EngineStateKt
import kotlinx.coroutines.*
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.StateFlow

class AppViewModel {
    private val bridge = EngineBridge().also { it.start() }
    private val scope = CoroutineScope(Dispatchers.Default + SupervisorJob())
    private val _state = MutableStateFlow(bridge.snapshot())
    val state: StateFlow<EngineStateKt> = _state

    init {
        scope.launch {
            while (isActive) {
                _state.value = bridge.snapshot()
                delay(33) // ~30 fps poll
            }
        }
    }

    fun dispose() {
        scope.cancel()
        bridge.stop()
        bridge.close()
    }

    // --- Commands ---
    fun startRecording(bank: Int, x: Float, y: Float) =
        bridge.submit(CT.StartRecording, bank = bank, fx = x, fy = y)
    fun stopRecording() = bridge.submit(CT.StopRecording)
    fun tapSample(id: Long) = bridge.submit(CT.TapSample, sampleId = id)
    fun deleteSample(id: Long) = bridge.submit(CT.DeleteSample, sampleId = id)
    fun setSampleVolume(id: Long, v: Float) = bridge.submit(CT.SetSampleVolume, sampleId = id, fvalue = v)
    fun setSampleReverse(id: Long, on: Boolean) = bridge.submit(CT.SetSampleReverse, sampleId = id, bvalue = on)
    fun setSampleSpeed(id: Long, speed: Int) = bridge.submit(CT.SetSampleSpeed, sampleId = id, ivalue = speed)
    fun moveSample(id: Long, bank: Int, x: Float, y: Float) =
        bridge.submit(CT.MoveSample, sampleId = id, bank = bank, fx = x, fy = y)
    fun setBankMute(bank: Int, muted: Boolean) = bridge.submit(CT.SetBankMute, bank = bank, bvalue = muted)
    fun setBankVolume(bank: Int, v: Float) = bridge.submit(CT.SetBankVolume, bank = bank, fvalue = v)
    fun setBankMode(bank: Int, oneShot: Boolean) =
        bridge.submit(CT.SetBankMode, bank = bank, ivalue = if (oneShot) 1 else 0)
    fun fireAllInBank(bank: Int) = bridge.submit(CT.FireAllInBank, bank = bank)
    fun captureLastLoop(bank: Int) = bridge.submit(CT.CaptureLastLoop, bank = bank)
    fun setBeatsPerBar(value: Int) = bridge.submit(CT.SetBeatsPerBar, ivalue = value)
    fun reset() = bridge.submit(CT.Reset)
    fun setMasterPlaying(playing: Boolean) =
        bridge.submit(CT.SetMasterPlaying, bvalue = playing)
    fun setBankSolo(bank: Int, soloed: Boolean) =
        bridge.submit(CT.SetBankSolo, bank = bank, bvalue = soloed)
    fun setSampleStartOffset(id: Long, frames: Int) =
        bridge.submit(com.taplooper.bridge.CommandType.SetSampleStartOffset, sampleId = id, ivalue = frames)
    fun setMasterBarFrames(frames: Int) =
        bridge.submit(com.taplooper.bridge.CommandType.SetMasterBarFrames, ivalue = frames)
    fun setSampleRepeat(id: Long, repeat: Boolean) =
        bridge.submit(com.taplooper.bridge.CommandType.SetSampleRepeat, sampleId = id, bvalue = repeat)
    fun setExtraLatencyFrames(frames: Int) =
        bridge.submit(com.taplooper.bridge.CommandType.SetExtraLatencyFrames, ivalue = frames)
}
