#include "rolling_buffer.h"
#include <gtest/gtest.h>
#include <vector>

using namespace tl;

TEST(RollingBuffer, WriteThenReadLastReturnsLatest) {
    RollingBuffer buf(/*capacity_frames=*/8, /*channels=*/2);
    std::vector<float> in(16);                  // 8 frames stereo
    for (int i = 0; i < 16; ++i) in[i] = float(i);
    buf.write(in.data(), 8);

    std::vector<float> out(16, 0.f);
    buf.read_last(out.data(), 8);
    EXPECT_EQ(out, in);
}

TEST(RollingBuffer, WrapAround) {
    RollingBuffer buf(/*capacity_frames=*/4, /*channels=*/1);
    std::vector<float> a = {1, 2, 3, 4};
    std::vector<float> b = {5, 6, 7, 8};
    buf.write(a.data(), 4);
    buf.write(b.data(), 4);   // should overwrite a
    std::vector<float> out(4, 0.f);
    buf.read_last(out.data(), 4);
    EXPECT_EQ(out, b);
}

TEST(RollingBuffer, PartialReadOlderFramesLeading) {
    RollingBuffer buf(/*capacity_frames=*/4, /*channels=*/1);
    std::vector<float> a = {1, 2, 3, 4, 5, 6}; // 6 frames into a 4-frame buffer
    buf.write(a.data(), 6);  // buffer now contains 3,4,5,6
    std::vector<float> out(4, 0.f);
    buf.read_last(out.data(), 4);
    EXPECT_EQ(out, (std::vector<float>{3, 4, 5, 6}));
}
