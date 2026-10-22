//
// Copyright (c) 2018 Ampere Computing. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause
//
#include <gtest/gtest.h>
#include <gmock/gmock.h>
#include "ampere/sc/pipeline.h"
#include "ampere/sc/testing/test.h"
#include "ampere/sc/testing/test_module.h"

using namespace ampere::sc;

using ::testing::ElementsAre;
using ::testing::IsEmpty;

struct PipelineTestMixin
{
    int cycle{0};
    Pipeline<int> *pipe_ref = nullptr;

    auto items() const { return std::vector(pipe_ref->begin(), pipe_ref->end()); }

    void run()
    {
        pipe_ref->run();
        ++cycle;
    }

    void run_for(size_t cycle_count)
    {
        for (size_t i = 0; i < cycle_count; i++)
            run();
    }

    PipelineTestMixin() = default;
    explicit PipelineTestMixin(Pipeline<int> *pipe) : pipe_ref(pipe) {}
};

bool a(Pipeline<int>::Data data)
{
    data = 42;
    int d = data;
    EXPECT_EQ(data.get(), 42);
    EXPECT_EQ(d, 42);
    return true;
}

bool b(Pipeline<int>::Data data)
{
    EXPECT_EQ(data.get(), 42);
    return true;
}

TEST(NoStagePipelineTest, EmptyPipelineRunsAndDoesNothing)
{
    Pipeline<int> pipe{};
    pipe.run();
    EXPECT_TRUE(pipe.empty());
}

struct SingleStagePipelineTest : ::testing::Test
{
    Pipeline<int> pipe{a};
};

TEST_F(SingleStagePipelineTest, InitiallyIsEmpty)
{
    EXPECT_TRUE(pipe.empty());
}

TEST_F(SingleStagePipelineTest, WritesDataWhenRun)
{
    pipe.run();
    EXPECT_TRUE(pipe.empty());
}

struct PipelineTest : ::testing::Test, PipelineTestMixin
{
    bool a(Pipeline<int>::Data data)
    {
        if (!input.empty()) {
            data = input.front();
            input.pop_front();
        }

        return true;
    }

    bool b(Pipeline<int>::Data data)
    {
        output.push_back(data);
        return true;
    }

    std::deque<int> input;
    std::deque<int> output;

    PipelineTest() : PipelineTestMixin(&pipe) {}

    Pipeline<int> pipe{std::bind_front(&PipelineTest::a, this), std::bind_front(&PipelineTest::b, this)};
};

TEST_F(PipelineTest, TwoStagePipeline)
{
    input = {1, 2, 3};
    std::deque<int> expected;

    run();
    EXPECT_TRUE(output.empty());
    run();
    EXPECT_EQ(output.size(), 1u);
    EXPECT_EQ(output.front(), 1);
    run();
    EXPECT_EQ(output.size(), 2u);
    expected = {1, 2};
    EXPECT_EQ(output, expected);
    run();
    expected = {1, 2, 3};
    EXPECT_EQ(output, expected);
}

TEST_F(PipelineTest, ArrayIndexOperator)
{
    input = {1, 2, 3};
    auto expected = input;

    EXPECT_FALSE(pipe[0].initialized());
    EXPECT_FALSE(pipe[1].initialized());

    for (auto input_item : expected) {
        run();
        EXPECT_FALSE(pipe[0].initialized());
        EXPECT_TRUE(pipe[1].initialized());
        EXPECT_EQ(pipe[1].get(), input_item);
    }
}

TEST_F(PipelineTest, Iterator)
{
    input = {1, 2, 3};
    EXPECT_THAT(items(), IsEmpty());

    auto values = input;
    for (auto i : values) {
        run();
        EXPECT_THAT(items(), ElementsAre(i));
    }
}

struct AssignPipelineTest : ::testing::Test, PipelineTestMixin
{
    bool a(Pipeline<int>::Data data)
    {
        data = 5;
        data = 4;
        values.emplace_back(data);
        return true;
    }

    bool b(Pipeline<int>::Data data)
    {
        data = 3;
        values.emplace_back(data);
        return true;
    }

    AssignPipelineTest() : PipelineTestMixin(&pipe) {}

    Pipeline<int> pipe{std::bind_front(&AssignPipelineTest::a, this), std::bind_front(&AssignPipelineTest::b, this)};
    std::vector<int> values;
};

TEST_F(AssignPipelineTest, Assignment)
{
    run();
    EXPECT_THAT(values, ElementsAre(4));
    run();
    EXPECT_THAT(values, ElementsAre(4, 3, 4));
}

struct StallingPipeline : ::testing::Test, PipelineTestMixin
{
    bool a(Pipeline<int>::Data data)
    {
        if (cycle < 3)
            return false;
        data = 16;
        return true;
    }

    bool b(Pipeline<int>::Data data)
    {
        if (received_cycle == -1) {
            received_data = data;
            received_cycle = cycle;
        }
        return true;
    }

    int received_data{-1};
    int received_cycle{-1};

    StallingPipeline() : PipelineTestMixin(&pipe) {}

    Pipeline<int> pipe{std::bind_front(&StallingPipeline::a, this), std::bind_front(&StallingPipeline::b, this)};
};

TEST_F(StallingPipeline, StageOneCanDelayStageTwo)
{
    run_for(10);

    EXPECT_EQ(received_cycle, 4);
    EXPECT_EQ(received_data, 16);
}

struct StallingPipeline2 : ::testing::Test, PipelineTestMixin
{
    bool a(Pipeline<int>::Data data)
    {
        data = data_value++;
        results_from_a.push_back(data);
        return true;
    }

    bool b(Pipeline<int>::Data data) { return cycle < 4; }

    int data_value{100};

    std::vector<int> results_from_a;

    StallingPipeline2() : PipelineTestMixin(&pipe) {}

    Pipeline<int> pipe{std::bind_front(&StallingPipeline2::a, this), std::bind_front(&StallingPipeline2::b, this)};
};

TEST_F(StallingPipeline2, StageTwoCanStallStageOne)
{
    run_for(8);
    EXPECT_THAT(results_from_a, ElementsAre(100, 101, 102, 103));
}

struct StallingPipeline3 : ::testing::Test, PipelineTestMixin
{
    bool a(Pipeline<int>::Data data)
    {
        data = cycle;
        results_from_a.push_back(cycle);
        return true;
    }

    bool b(Pipeline<int>::Data data)
    {
        results_from_b.push_back(data);
        // stall for cycles 3 and 4
        return cycle != 3 && cycle != 4;
    }

    std::vector<int> results_from_a;
    std::vector<int> results_from_b;

    StallingPipeline3() : PipelineTestMixin(&pipe) {}

    Pipeline<int> pipe{std::bind_front(&StallingPipeline3::a, this), std::bind_front(&StallingPipeline3::b, this)};
};

TEST_F(StallingPipeline3, ClearWhileStalled)
{
    run_for(4);   // cycles 0, 1, 2, 3
    pipe.clear(); // clear between cycles 3 & 4
    run_for(4);   // cycles 4, 5, 6, 7

    EXPECT_THAT(results_from_a, ElementsAre(0, 1, 2, /*stalled for 3 by b*/ 4, 5, 6, 7));
    EXPECT_THAT(results_from_b, ElementsAre(0, 1, 2, /*3 cleared*/ 4, 5, 6));
}

/*
 * Test that calling invalidate() on each stage is equivalent to clear()
 */
TEST_F(StallingPipeline3, InvalidateAllWhileStalled)
{
    run_for(4); // cycles 0, 1, 2, 3

    for (uint64_t i = 0; i < pipe.size(); i++) {
        auto stage = pipe[i];
        stage.invalidate();
    }

    run_for(4); // cycles 4, 5, 6, 7

    EXPECT_THAT(results_from_a, ElementsAre(0, 1, 2, /*stalled for 3 by b*/ 4, 5, 6, 7));
    EXPECT_THAT(results_from_b, ElementsAre(0, 1, 2, /*3 cleared*/ 4, 5, 6));
}

struct StretchedPipeline : Module, PipelineTestMixin
{
    uint64_t gap = 1;
    StretchedPipeline(const sc_core::sc_module_name &name, uint64_t gap = 1)
        : Module{name}, PipelineTestMixin(&pipe), gap{gap}
    {
    }

    bool a(Pipeline<int>::Data data)
    {
        if (get_cycle() % gap == 0)
            data = 12;
        return true;
    }

    bool b(Pipeline<int>::Data data)
    {
        if (received_cycle == -1) {
            received_data = data;
            received_cycle = get_cycle();
        }
        return true;
    }

    int received_data{-1};
    int received_cycle{-1};

    Pipeline<int> pipe{
        "pipe",
        {{"a", std::bind_front(&StretchedPipeline::a, this)}, {"b", std::bind_front(&StretchedPipeline::b, this)}}};

    bool run() override
    {
        pipe.run();
        return true;
    }
};

struct StretchedPipelineTest : Test
{
    std::unique_ptr<StretchedPipeline> top;

    StretchedPipelineTest(uint64_t gap = 1)
    {
        get_environment()->parameters.add_setting("top.pipe.b.index", 3, "test bench");
        top = std::make_unique<StretchedPipeline>("top", gap);
    }
};

TEST_F(StretchedPipelineTest, FourCyclePipelineTakesFourCycles)
{
    tick(8);

    EXPECT_EQ(top->received_cycle, 3);
    EXPECT_EQ(top->received_data, 12);
}

TEST_F(StretchedPipelineTest, Iterator)
{
    tick();
    EXPECT_THAT(top->items(), ElementsAre(12));

    tick();
    EXPECT_THAT(top->items(), ElementsAre(12, 12));

    tick();
    EXPECT_THAT(top->items(), ElementsAre(12, 12, 12));

    tick();
    EXPECT_THAT(top->items(), ElementsAre(12, 12, 12));
}

struct StretchedPipelineWithGapTest : StretchedPipelineTest
{
    StretchedPipelineWithGapTest() : StretchedPipelineTest(2) {}
};

TEST_F(StretchedPipelineWithGapTest, IteratorSkips)
{
    tick();
    EXPECT_THAT(top->items(), ElementsAre(12));

    tick();
    EXPECT_THAT(top->items(), ElementsAre(12));

    tick();
    EXPECT_THAT(top->items(), ElementsAre(12, 12));

    tick();
    EXPECT_THAT(top->items(), ElementsAre(12));

    tick();
    EXPECT_THAT(top->items(), ElementsAre(12, 12));
}

struct StallingPipeline4 : public Module
{
    StallingPipeline4(const sc_core::sc_module_name &name,
                      std::set<int> stall_a,
                      std::set<int> stall_b,
                      std::set<int> stall_c)
        : Module{name}, stall_a{stall_a}, stall_b{stall_b}, stall_c{stall_c}
    {
    }

    bool a(Pipeline<int>::Data data)
    {
        if (stall_a.contains(get_cycle()))
            return false;
        data = get_cycle();
        return true;
    }

    bool b(Pipeline<int>::Data data) { return !stall_b.contains(get_cycle()); }

    bool c(Pipeline<int>::Data data) { return !stall_c.contains(get_cycle()); }

    Pipeline<int> pipe{"pipe",
                       {
                           AMPERE_PIPE_STAGE_M(StallingPipeline4, a),
                           AMPERE_PIPE_STAGE_M(StallingPipeline4, b),
                           AMPERE_PIPE_STAGE_M(StallingPipeline4, c),
                       }};

    std::set<int> stall_a;
    std::set<int> stall_b;
    std::set<int> stall_c;

    bool run() override
    {
        pipe.run();
        return true;
    }
};

struct BubbleCollapsingPipelineTest : TestModule
{
    std::unique_ptr<StallingPipeline4> top;

    BubbleCollapsingPipelineTest(bool collapse = true) : TestModule{{"top.pipe.collapse_bubbles", collapse}}
    {
        top = std::make_unique<StallingPipeline4>("top",
                                                  std::set<int>{3}, // cycles to stall stage a
                                                  std::set<int>{},  // cycles to stall stage b
                                                  std::set<int>{4}  // cycles to stall stage c
        );
    }
};

TEST_F(BubbleCollapsingPipelineTest, CatchUpAfterStall)
{
    // Run with no stalls for cycles 0, 1, 2
    tick(3);
    EXPECT_EQ(top->pipe[1].get(), 2); // stage b
    EXPECT_EQ(top->pipe[2].get(), 1); // stage c

    // Stall stage a for cycle 3 to empty stage b
    tick(1);
    EXPECT_FALSE(top->pipe[1].initialized()); // stage b
    EXPECT_EQ(top->pipe[2].get(), 2);         // stage c

    // Stall only stage c for cycle 4, test that stages a/b catch up
    tick(1);
    EXPECT_TRUE(top->pipe[1].initialized()); // stage b
    EXPECT_EQ(top->pipe[1].get(), 4);        // stage b
    EXPECT_EQ(top->pipe[2].get(), 2);        // stage c

    // Run with no stalls for cycle 5
    tick(1);
    EXPECT_EQ(top->pipe[1].get(), 5); // stage b
    EXPECT_EQ(top->pipe[2].get(), 4); // stage c
}

struct NotCollapsingBubblesPipelineTest : public BubbleCollapsingPipelineTest
{
    NotCollapsingBubblesPipelineTest() : BubbleCollapsingPipelineTest{false} {}
};

TEST_F(NotCollapsingBubblesPipelineTest, DontCatchUpAfterStall)
{
    // Run with no stalls for cycles 0, 1, 2
    tick(3);
    EXPECT_EQ(top->pipe[1].get(), 2); // stage b
    EXPECT_EQ(top->pipe[2].get(), 1); // stage c

    // Stall stage a for cycle 3 to empty stage b
    tick(1);
    EXPECT_FALSE(top->pipe[1].initialized()); // stage b
    EXPECT_EQ(top->pipe[2].get(), 2);         // stage c

    // Stall only stage c for cycle 4, test that stages a/b do not catch up
    tick(1);
    EXPECT_FALSE(top->pipe[1].initialized()); // stage b
    EXPECT_EQ(top->pipe[2].get(), 2);         // stage c

    // Run with no stalls for cycle 5
    tick(1);
    EXPECT_EQ(top->pipe[1].get(), 5);         // stage b
    EXPECT_FALSE(top->pipe[2].initialized()); // stage c

    // Run with no stalls for cycle 6
    tick(1);
    EXPECT_EQ(top->pipe[1].get(), 6); // stage b
    EXPECT_EQ(top->pipe[2].get(), 5); // stage c
}

struct GroupedPipeline : Module
{
    template<typename P, typename Q>
    GroupedPipeline(
        const sc_core::sc_module_name &name, bool cross_stage_stall, unsigned width, P comp_data_func, Q comp_pipe_func)
        : Module{name},
          pipe{"pipe",
               {{"a", std::bind_front(&GroupedPipeline::a, this)},
                {"b", std::bind_front(&GroupedPipeline::b, this)},
                {"c", std::bind_front(&GroupedPipeline::c, this)}},
               cross_stage_stall,
               width,
               comp_data_func,
               comp_pipe_func}
    {
    }

    GroupedPipeline(const sc_core::sc_module_name &name) : GroupedPipeline(name, false, 2, nullptr, nullptr) {}

    PipelineGroup<int> pipe;

    unsigned cycle = 0;
    int index = 0;
    std::vector<int> ran;
    std::vector<int> complete;

    bool a(Pipeline<int>::Data data)
    {
        if (index % 2 == 1 && cycle % 4 == 3)
            return false;

        data = index++;
        ran.emplace_back(data);
        return true;
    }
    bool b(Pipeline<int>::Data data)
    {
        ran.emplace_back(data);
        return true;
    }
    bool c(Pipeline<int>::Data data)
    {
        ran.emplace_back(data);
        if (data % 2 == 1 && cycle % 4 == 0)
            return false;

        complete.emplace_back(data);
        return true;
    }

    void pipe_run()
    {
        ran.clear();
        complete.clear();
        pipe.run();
        ++cycle;
    }
};

struct PipelineGroupTest : TestModule
{
    PipelineGroupTest(bool collapse = false) : TestModule{{"top.pipe.collapse_bubbles", collapse}} {}

    GroupedPipeline top{"top"};
};

TEST_F(PipelineGroupTest, Width)
{
    EXPECT_EQ(top.pipe.width(), 2);
}

TEST_F(PipelineGroupTest, Size)
{
    EXPECT_EQ(top.pipe.size(), 3);
}

TEST_F(PipelineGroupTest, CanRun)
{
    top.pipe_run();
    EXPECT_THAT(top.ran, ElementsAre(0, 1));

    top.pipe_run();
    EXPECT_THAT(top.ran, ElementsAre(0, 1, 2, 3));
    EXPECT_THAT(top.complete, ElementsAre());

    top.pipe_run();
    EXPECT_THAT(top.ran, ElementsAre(0, 1, 2, 3, 4, 5));
    EXPECT_THAT(top.complete, ElementsAre(0, 1));

    top.pipe_run();
    EXPECT_THAT(top.ran, ElementsAre(2, 3, 4, 5, 6));
    EXPECT_THAT(top.complete, ElementsAre(2, 3));

    top.pipe_run();
    EXPECT_THAT(top.ran, ElementsAre(4, 5, 6, 7));
    EXPECT_THAT(top.complete, ElementsAre(4));

    top.pipe_run();
    EXPECT_THAT(top.ran, ElementsAre(6, 5, 7, 8, 9));
    EXPECT_THAT(top.complete, ElementsAre(6, 5));
}

TEST_F(PipelineGroupTest, Clear)
{
    top.pipe_run();
    top.pipe_run();
    top.pipe.clear();

    top.pipe_run();
    EXPECT_THAT(top.complete, IsEmpty());
}

TEST_F(PipelineGroupTest, CanIndex)
{
    top.pipe_run();
    EXPECT_EQ(top.pipe[0][1], 0);
    EXPECT_EQ(top.pipe[1][1], 1);
}

TEST_F(PipelineGroupTest, CanIterate)
{
    EXPECT_EQ(top.pipe.begin(), top.pipe.end());

    top.pipe_run();
    EXPECT_THAT(std::vector(top.pipe.begin(), top.pipe.end()), ElementsAre(0, 1));

    top.pipe_run();
    EXPECT_THAT(std::vector(top.pipe.begin(), top.pipe.end()), ElementsAre(0, 2, 1, 3));
}

struct PipelineGroupCollapsingTest : PipelineGroupTest
{
    PipelineGroupCollapsingTest() : PipelineGroupTest(true) {}
};

TEST_F(PipelineGroupCollapsingTest, CanRun)
{
    for (unsigned i = 0; i < 4; ++i)
        top.pipe_run();

    EXPECT_THAT(top.ran, ElementsAre(2, 3, 4, 5, 6));
    EXPECT_THAT(top.complete, ElementsAre(2, 3));

    top.pipe_run();
    EXPECT_THAT(top.ran, ElementsAre(4, 5, 6, 7, 8));
    EXPECT_THAT(top.complete, ElementsAre(4));

    top.pipe_run();
    EXPECT_THAT(top.ran, ElementsAre(6, 5, 7, 8, 9, 10));
    EXPECT_THAT(top.complete, ElementsAre(6, 5));
}

struct PipelineGroupCompareDataTest : Test
{
    GroupedPipeline top{"top", false, 2, [](const int &a, const int &b) { return a < b; }, nullptr};
};

TEST_F(PipelineGroupCompareDataTest, CanRun)
{
    for (unsigned i = 0; i < 4; ++i)
        top.pipe_run();

    EXPECT_THAT(top.ran, ElementsAre(2, 3, 4, 5, 6));
    EXPECT_THAT(top.complete, ElementsAre(2, 3));

    top.pipe_run();
    EXPECT_THAT(top.ran, ElementsAre(4, 5, 6, 7));
    EXPECT_THAT(top.complete, ElementsAre(4));

    top.pipe_run();
    EXPECT_THAT(top.ran, ElementsAre(5, 6, 7, 8, 9));
    EXPECT_THAT(top.complete, ElementsAre(5, 6));
}

struct PipelineGroupComparePipeTest : Test
{
    GroupedPipeline top{"top", false, 2, nullptr, [](const Pipeline<int> &a, const Pipeline<int> &b) {
                            // This comparison function will run the pipe with the *largest* entry
                            // in the latest stage first (to intentionally mix up the run order)
                            for (size_t s = a.size() - 1; s > 0; s--) {
                                if (a[s].initialized() && b[s].initialized()) {
                                    return a[s].get() > b[s].get();
                                } else if (a[s].initialized() || b[s].initialized()) {
                                    return b[s].initialized();
                                }
                            }
                            return true;
                        }};
};

TEST_F(PipelineGroupComparePipeTest, CanRun)
{
    for (unsigned i = 0; i < 4; ++i)
        top.pipe_run();

    EXPECT_THAT(top.ran, ElementsAre(3, 2, 5, 4, 6));
    EXPECT_THAT(top.complete, ElementsAre(3, 2));

    top.pipe_run();
    EXPECT_THAT(top.ran, ElementsAre(5, 4, 7));
    EXPECT_THAT(top.complete, ElementsAre(4));

    top.pipe_run();
    EXPECT_THAT(top.ran, ElementsAre(5, 7, 6, 8, 9));
    EXPECT_THAT(top.complete, ElementsAre(5));
}

struct PipelineGroupCrossStageStallTest : Test
{
    GroupedPipeline top{"top", true, 3, nullptr, nullptr};
};

TEST_F(PipelineGroupCrossStageStallTest, StallPropegatesToOtherPipelines)
{
    top.pipe_run();
    EXPECT_THAT(top.ran, ElementsAre(0, 1, 2));

    top.pipe_run();
    EXPECT_THAT(top.ran, ElementsAre(0, 1, 2, 3, 4, 5));
    EXPECT_THAT(top.complete, ElementsAre());

    top.pipe_run();
    EXPECT_THAT(top.ran, ElementsAre(0, 1, 2, 3, 4, 5, 6, 7, 8));
    EXPECT_THAT(top.complete, ElementsAre(0, 1, 2));

    // Pipeline 0, stage a stalls
    top.pipe_run();
    EXPECT_THAT(top.ran, ElementsAre(3, 4, 5, 6, 7, 8));
    EXPECT_THAT(top.complete, ElementsAre(3, 4, 5));

    // Pipeline 1, stage c stalls
    top.pipe_run();
    EXPECT_THAT(top.ran, ElementsAre(6, 7, 9));
    EXPECT_THAT(top.complete, ElementsAre(6));

    top.pipe_run();
    EXPECT_THAT(top.ran, ElementsAre(7, 8, 9, 10, 11, 12));
    EXPECT_THAT(top.complete, ElementsAre(7, 8));
}
