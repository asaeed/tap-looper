#include "audio_engine.h"
#include "commands.h"
#include <gtest/gtest.h>
#include <vector>

using namespace tl;

// Helper: pass `frames` of input through the engine, discarding output.
static void tick(AudioEngine& e, int frames, float input_value = 0.f) {
    std::vector<float> in(frames * kChannels, input_value);
    std::vector<float> out(frames * kChannels, 0.f);
    e.process(in.data(), out.data(), frames);
}

TEST(AudioEngine, InitialStateIsClean) {
    AudioEngine e;
    auto s = e.snapshot();
    EXPECT_FALSE(s.recording);
    EXPECT_EQ(s.master_bar_frames, 0u);
    EXPECT_EQ(s.sample_count, 0);
}

TEST(AudioEngine, FirstRecordingSetsMasterBarFrames) {
    AudioEngine e;
    Command c{}; c.type = CommandType::StartRecording; c.bank = 0; c.fx = 0.5f; c.fy = 0.5f;
    ASSERT_TRUE(e.submit(c));
    tick(e, 256, 0.1f);     // consume START_RECORDING
    auto s1 = e.snapshot();
    EXPECT_TRUE(s1.recording);

    // Tick for enough frames to make recording > 200ms
    tick(e, 48000 / 2, 0.1f); // 0.5 seconds at 48kHz

    Command stop{}; stop.type = CommandType::StopRecording;
    ASSERT_TRUE(e.submit(stop));
    tick(e, 256);

    auto s2 = e.snapshot();
    EXPECT_FALSE(s2.recording);
    EXPECT_EQ(s2.sample_count, 1);
    // L = (256 + 24000) frames approximately
    EXPECT_GT(s2.master_bar_frames, 24000u);
    EXPECT_LT(s2.master_bar_frames, 25000u);
}

TEST(AudioEngine, RecordingShorterThanMinDiscarded) {
    AudioEngine e;
    Command c{}; c.type = CommandType::StartRecording; c.bank = 0;
    e.submit(c);
    tick(e, 256);
    // less than 200ms = less than 9600 frames at 48kHz
    tick(e, 1000);
    Command stop{}; stop.type = CommandType::StopRecording;
    e.submit(stop);
    tick(e, 256);

    auto s = e.snapshot();
    EXPECT_EQ(s.sample_count, 0);
    EXPECT_EQ(s.master_bar_frames, 0u);
}

TEST(AudioEngine, GlobalPlayHeadAdvances) {
    AudioEngine e;
    tick(e, 1000);
    auto s = e.snapshot();
    // play head is internal but we can verify indirectly via subsequent offset tests in Task 13.
    // For now just verify nothing crashes.
    SUCCEED();
}

TEST(AudioEngine, LoopSampleEnabledAfterFirstRecording) {
    AudioEngine e;
    Command c{}; c.type = CommandType::StartRecording; c.bank = 0;
    e.submit(c); tick(e, 256, 0.1f);
    tick(e, 48000 / 2, 0.1f);
    Command stop{}; stop.type = CommandType::StopRecording;
    e.submit(stop); tick(e, 256);
    auto s = e.snapshot();
    ASSERT_EQ(s.sample_count, 1);
    EXPECT_TRUE(s.samples[0].enabled);   // LOOP bank → enabled on stop
}
