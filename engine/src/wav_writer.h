#pragma once
#include "types.h"

namespace tl {
// Write interleaved float32 PCM as a WAV file (format 3 = IEEE float).
// Returns false if the file could not be written.
bool write_wav_f32(const char* path, const float* interleaved,
                   Frames frames, int channels, int sample_rate);
} // namespace tl
