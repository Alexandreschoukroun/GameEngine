#include <doctest/doctest.h>

#include "core/time.h"

TEST_CASE("FixedTimestepAccumulator consumes exact multiples of the fixed step") {
    core::FixedTimestepAccumulator accumulator(1.0 / 60.0);

    accumulator.addFrameTime(2.0 / 60.0);

    CHECK(accumulator.consumeStep());
    CHECK(accumulator.consumeStep());
    CHECK_FALSE(accumulator.consumeStep());
}

TEST_CASE("FixedTimestepAccumulator keeps the remainder across frames") {
    core::FixedTimestepAccumulator accumulator(1.0 / 60.0);

    accumulator.addFrameTime(0.5 / 60.0);
    CHECK_FALSE(accumulator.consumeStep());

    accumulator.addFrameTime(0.5 / 60.0);
    CHECK(accumulator.consumeStep());
    CHECK_FALSE(accumulator.consumeStep());
}

TEST_CASE("FixedTimestepAccumulator clamps huge frame times to avoid the spiral of death") {
    core::FixedTimestepAccumulator accumulator(1.0 / 60.0, 0.25);

    accumulator.addFrameTime(10.0);

    int steps = 0;
    while (accumulator.consumeStep()) {
        ++steps;
    }

    CHECK(steps == 15);
}
