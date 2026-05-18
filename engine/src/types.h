#pragma once
#include <cstdint>

namespace tl {
using SampleId = uint64_t;
using BankId   = uint8_t;
using Frames   = uint32_t;

constexpr int  kNumBanks         = 8;
constexpr int  kMaxSamples       = 64;
constexpr int  kSampleRate       = 48000;
constexpr int  kChannels         = 2;          // stereo output
constexpr Frames kMaxRecFrames   = kSampleRate * 60; // 60s preallocated for active recording
constexpr int  kMinRecordingMs   = 200;
constexpr Frames kFallbackBufferFrames = kSampleRate * 8; // pre-L rolling buffer fallback
}
