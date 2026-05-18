#include "bpm.h"
#include <gtest/gtest.h>

using namespace tl;

TEST(Bpm, FourBeatsPerBarAt48kHz_2sBar_Gives120Bpm) {
    // 2-second bar at 48kHz = 96000 frames. 4 beats/bar => 120 bpm.
    EXPECT_NEAR(derive_bpm(/*bar_frames=*/96000, /*beats_per_bar=*/4, /*sample_rate=*/48000),
                120.0, 0.01);
}

TEST(Bpm, EightBeatsPerBarDoublesBpm) {
    EXPECT_NEAR(derive_bpm(96000, 8, 48000), 240.0, 0.01);
}

TEST(Bpm, ZeroBarFramesReturnsZero) {
    EXPECT_EQ(derive_bpm(0, 4, 48000), 0.0);
}
