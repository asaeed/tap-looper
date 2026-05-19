#pragma once
#include "types.h"

namespace tl {
enum class CommandType : uint8_t {
  StartRecording,    // bank, x, y
  StopRecording,
  TapSample,         // sample_id (toggles enable for LOOP, triggers for ONE_SHOT)
  DeleteSample,      // sample_id
  SetSampleVolume,   // sample_id, fvalue (0..1)
  SetSampleReverse,  // sample_id, bvalue
  SetSampleSpeed,    // sample_id, ivalue (0=Half,1=Normal,2=Double)
  MoveSample,        // sample_id, bank, x, y
  SetBankMute,       // bank, bvalue
  SetBankVolume,     // bank, fvalue
  SetBankMode,       // bank, ivalue (0=LOOP,1=ONE_SHOT)
  FireAllInBank,     // bank
  CaptureLastLoop,   // bank (focused)
  SetBeatsPerBar,    // ivalue (2/4/8)
  Reset,             // wipes all samples, resets master_bar_frames, etc.
  SetMasterPlaying,  // bvalue: true=play, false=stop
  SetBankSolo,       // bank, bvalue
};

struct Command {
  CommandType type;
  SampleId sample_id;
  BankId bank;
  float fx, fy;
  float fvalue;
  int ivalue;
  bool bvalue;
};
} // namespace tl
