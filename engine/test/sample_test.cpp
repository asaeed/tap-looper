#include "sample.h"
#include <gtest/gtest.h>

using namespace tl;

TEST(Sample, NewSampleHasDefaults) {
    Sample s(/*id=*/42, /*bank=*/2, /*capacity_frames=*/kMaxRecFrames, /*channels=*/kChannels);
    EXPECT_EQ(s.id, 42u);
    EXPECT_EQ(s.bank, 2);
    EXPECT_EQ(s.recorded_length_frames, 0u);
    EXPECT_EQ(s.recorded_offset_in_bar, 0u);
    EXPECT_FLOAT_EQ(s.volume, 1.0f);
    EXPECT_FALSE(s.reverse);
    EXPECT_EQ(s.speed, Speed::Normal);
    EXPECT_FALSE(s.enabled);   // not playing yet
    EXPECT_FALSE(s.recording);
}

TEST(Sample, AppendDuringRecordingIncreasesLength) {
    Sample s(1, 0, kMaxRecFrames, 1);
    s.recording = true;
    float frames[] = {1, 2, 3, 4};
    s.append(frames, 4);
    EXPECT_EQ(s.recorded_length_frames, 4u);
    EXPECT_FLOAT_EQ(s.pcm[0], 1.0f);
    EXPECT_FLOAT_EQ(s.pcm[3], 4.0f);
}

TEST(Sample, AppendStopsAtCapacity) {
    Sample s(1, 0, /*capacity_frames=*/2, 1);
    s.recording = true;
    float frames[] = {1, 2, 3, 4};
    s.append(frames, 4);
    EXPECT_EQ(s.recorded_length_frames, 2u);   // capped
}
