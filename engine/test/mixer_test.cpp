#include "mixer.h"
#include "sample.h"
#include "state_snapshot.h"
#include <gtest/gtest.h>
#include <vector>

using namespace tl;


TEST(Mixer, EnabledLoopSampleAtOffsetZero_PlaysFromStart) {
    // Create a sample with known PCM, 4 frames mono (for simplicity treat as stereo with channels=2)
    Sample s(1, 0, /*cap=*/8, /*channels=*/2);
    s.recording = true;
    float pcm[] = {1, 1, 2, 2, 3, 3, 4, 4}; // 4 frames stereo
    s.append(pcm, 4);
    s.recording = false;
    s.enabled = true;
    s.recorded_offset_in_bar = 0;

    BankState bank{}; bank.id = 0; bank.mode = BankMode::Loop; bank.volume = 1.0f; bank.muted = false;
    std::vector<Sample*> samples = {&s};
    BankState banks[kNumBanks] = {bank, bank, bank, bank, bank, bank, bank, bank};

    std::vector<float> out(4 * 2, 0.f);
    Frames bar_frames = 4;
    Frames play_head = 0;
    mix(samples, banks, bar_frames, play_head, /*frame_count=*/4, out.data());
    EXPECT_FLOAT_EQ(out[0], 1.0f);
    EXPECT_FLOAT_EQ(out[6], 4.0f);
}

TEST(Mixer, MutedBankSilencesSample) {
    Sample s(1, 0, 4, 2);
    s.recording = true;
    float pcm[] = {1, 1, 2, 2, 3, 3, 4, 4};
    s.append(pcm, 4);
    s.recording = false;
    s.enabled = true;

    BankState bank{}; bank.id = 0; bank.mode = BankMode::Loop; bank.volume = 1.0f; bank.muted = true;
    std::vector<Sample*> samples = {&s};
    BankState banks[kNumBanks] = {bank};
    for (int i = 1; i < kNumBanks; ++i) { banks[i] = bank; banks[i].id = i; }

    std::vector<float> out(8, 0.f);
    mix(samples, banks, 4, 0, 4, out.data());
    for (float v : out) EXPECT_FLOAT_EQ(v, 0.0f);
}

TEST(Mixer, VolumeScaling) {
    Sample s(1, 0, 4, 2);
    s.recording = true;
    float pcm[] = {1, 1, 1, 1, 1, 1, 1, 1};
    s.append(pcm, 4);
    s.recording = false;
    s.enabled = true;
    s.volume = 0.5f;
    BankState bank{}; bank.mode = BankMode::Loop; bank.volume = 0.5f;
    std::vector<Sample*> samples = {&s};
    BankState banks[kNumBanks];
    for (int i = 0; i < kNumBanks; ++i) { banks[i] = bank; banks[i].id = i; }

    std::vector<float> out(8, 0.f);
    mix(samples, banks, 4, 0, 4, out.data());
    EXPECT_FLOAT_EQ(out[0], 0.25f); // 1 * 0.5 (sample) * 0.5 (bank)
}

TEST(Mixer, NearestMultipleOfLRoundsCorrectly) {
    EXPECT_EQ(nearest_multiple_of_L(4, 4), Frames(4));
    EXPECT_EQ(nearest_multiple_of_L(5, 4), Frames(8));   // multi-bar min = 2L = 8
    EXPECT_EQ(nearest_multiple_of_L(6, 4), Frames(8));
    EXPECT_EQ(nearest_multiple_of_L(10, 4), Frames(8));  // 10 closer to 8 than 12
    EXPECT_EQ(nearest_multiple_of_L(11, 4), Frames(12)); // 11 closer to 12
}
