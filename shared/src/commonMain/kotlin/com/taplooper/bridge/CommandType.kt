package com.taplooper.bridge

object CommandType {
    const val StartRecording   = 0
    const val StopRecording    = 1
    const val TapSample        = 2
    const val DeleteSample     = 3
    const val SetSampleVolume  = 4
    const val SetSampleReverse = 5
    const val SetSampleSpeed   = 6
    const val MoveSample       = 7
    const val SetBankMute      = 8
    const val SetBankVolume    = 9
    const val SetBankMode      = 10
    const val FireAllInBank    = 11
    const val CaptureLastLoop  = 12
    const val SetBeatsPerBar   = 13
    const val Reset            = 14
    const val SetMasterPlaying = 15
    const val SetBankSolo      = 16
}
