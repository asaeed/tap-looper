#include "state_snapshot.h"
#include <gtest/gtest.h>

using namespace tl;

TEST(EngineState, DefaultsAreFreshSession) {
    EngineState s;
    init_default_state(s);
    EXPECT_EQ(s.sample_count, 0);
    EXPECT_FALSE(s.recording);
    EXPECT_EQ(s.master_bar_frames, 0u);
    EXPECT_EQ(s.beats_per_bar, 4);
    EXPECT_FLOAT_EQ(s.bpm, 0.0f);
    EXPECT_EQ(s.focused_bank, 0);
    for (int i = 0; i < kNumBanks; ++i) {
        EXPECT_EQ(s.banks[i].id, BankId(i));
        EXPECT_EQ(s.banks[i].mode, BankMode::Loop);
        EXPECT_FLOAT_EQ(s.banks[i].volume, 1.0f);
        EXPECT_FALSE(s.banks[i].muted);
    }
}

TEST(StatePublisher, PublishedStateReadable) {
    StatePublisher pub;
    EngineState s;
    init_default_state(s);
    s.beats_per_bar = 8;
    s.version = 7;
    pub.publish(s);

    EngineState got = pub.snapshot();
    EXPECT_EQ(got.beats_per_bar, 8);
    EXPECT_EQ(got.version, 7u);
}
