#include "command_queue.h"
#include "commands.h"
#include <gtest/gtest.h>

using namespace tl;

TEST(CommandQueue, PushAndPopSingle) {
    CommandQueue<64> q;
    Command c{};
    c.type = CommandType::StartRecording;
    c.bank = 3;
    EXPECT_TRUE(q.push(c));
    Command out{};
    EXPECT_TRUE(q.pop(out));
    EXPECT_EQ(out.type, CommandType::StartRecording);
    EXPECT_EQ(out.bank, 3);
}

TEST(CommandQueue, FifoOrder) {
    CommandQueue<64> q;
    for (int i = 0; i < 10; ++i) {
        Command c{}; c.type = CommandType::TapSample; c.sample_id = i;
        q.push(c);
    }
    for (int i = 0; i < 10; ++i) {
        Command c{};
        EXPECT_TRUE(q.pop(c));
        EXPECT_EQ(c.sample_id, SampleId(i));
    }
}

TEST(CommandQueue, FullQueueReturnsFalse) {
    CommandQueue<4> q;
    Command c{}; c.type = CommandType::TapSample;
    EXPECT_TRUE(q.push(c));
    EXPECT_TRUE(q.push(c));
    EXPECT_TRUE(q.push(c));   // 3 items in 4-slot ring (one wasted to distinguish full vs empty)
    EXPECT_FALSE(q.push(c));  // full
}
