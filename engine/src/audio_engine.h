#pragma once
#include "commands.h"
#include "state_snapshot.h"
#include <atomic>
#include <memory>

namespace tl {
class AudioEngine {
public:
  AudioEngine();
  ~AudioEngine();

  // Real-time audio thread: process one buffer of stereo float frames.
  // `in` is interleaved stereo mic frames; `out` is interleaved stereo output.
  void process(const float* in, float* out, int frame_count);

  // Worker thread (or test driver): submit a command for processing.
  // Returns false if queue is full (rare, but possible under burst).
  bool submit(const Command& cmd);

  // UI thread: get a copy of the latest published state snapshot.
  EngineState snapshot() const;

  // Test/setup: get sample rate (default 48000)
  int sample_rate() const;

  // Audio I/O calls this once after device init to inform the engine of
  // the platform's round-trip latency (input buffer + output buffer in frames).
  // Used to compensate the recorded_offset_in_bar of subsequent recordings
  // so they play back in sync with what the user heard during recording.
  void set_round_trip_latency_frames(int frames);

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
} // namespace tl
