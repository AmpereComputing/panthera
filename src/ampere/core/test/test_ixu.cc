//
// Copyright (c) 2018-2019 Ampere Computing. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause
//

#include <gtest/gtest.h>
#include <queue>
#include <regex>

#include "ampere/archx/instruction_simple.h"
#include "ampere/core/ixu_defs.h"
#include "ampere/core/ixu.h"
#include "ampere/sc/testing/test_module.h"
#include "ampere/core/testing/uop_generator.h"
#include "ampere/core/testing/uop_consumer.h"
#include "ampere/sc/testing/clock_dummy.h"
#include "ampere/core/testing/mock_flush.h"
#include "ampere/util/enum.h"

#define NUM_EXECUTION_UNITS 31

namespace ampere {

using namespace core;
using namespace ::testing;

class IXUTest : public TestModule
{
    void SetUp() override { archx::Instruction::set_decoders({"furiosa"}); }

protected:
    ClockDummy clock;
    Environment env{"system",
                    {{"top.ixu_partitioned.num_execution_units", NUM_EXECUTION_UNITS},
                     {"top.rf_int.size", 40},
                     {"top.rf_fp.size", 40},
                     {"top.rf_flag.size", 40}}};
};

/* AllocatedUopGenerator is a specialized Uop generator where pseudo-uops can be specified with execution pipes,
 * partitions, and destination and source registers. This allows us to exercise salient aspects of the scheduler
 * without the level of indirection that using real uopcodes would require.
 *
 * To use the generator, you call `add_uop` with a string, e.g.:
 *
 *     add_uop("SchA0 regX0 regX1 regX2");
 *
 * The general form of the string is:
 *
 *     "Sch<pipe><partition>[.S|.F] [<dest> [<src1> [<src2>]]][ / <dest2>]"
 *
 *     <pipe>       - A pipe name (A, B, C, D, X, Y)
 *     <partition>  - A partition number (0, 1)
 *     .S           - Optional suffix meaning "sets flags"
 *     .F           - Optional suffix meaning "consumes flags"
 *     <dest>       - A destination register: regX0-regX31 or "_" for no destination
 *     <src1>       - The first source register: regX0-regX31, regV0-regV31, or "_" for no source
 *     <src2>       - The second source register: regX0-regX31, regV0-regV31,  or "_" for no source
 *     <dest2>      - Second destination (for simulating load pairs)
 *
 * Currently FP/SIMD registers are not supported.
 *
 * With this scheme, it's possible to arrange for various dependencies between uops. For example, here's two
 * completely independent uops, as they don't share pipes and don't have any src->dst dependencies:
 *
 *     add_uop("SchA0 regX0 regX1 regX2");
 *     add_uop("SchC1 regX3 regX4 regX5");
 *
 * On the other hand, these uops need to be serialized because of register/flag dependencies:
 *
 *     add_uop("SchA0 regX0 regX1");
 *     add_uop("SchB1.S regX3 regX5 regX0");  // needs X0 as a source
 *     add_uop("SchA1.F regX6");              // has a flag dependency on the previous uop
 */
struct AllocatedUopGenerator : public Taker<core::Uop>
{
    AllocatedUopGenerator()
    {
        rf_int.set_base_id(1);
        rf_fp.set_base_id(rf_int.get_size() + 1);
        rf_flag.set_base_id(rf_int.get_size() + rf_fp.get_size() + 1);
    }

    bool can_take() const override { return !uops.empty(); }
    uint64_t num_ready() const override { return uops.size(); }

    core::Uop take() override
    {
        auto uop = make_uop(uops.front());
        uops.pop();
        return uop;
    }

    const core::Uop& peek(uint64_t index = 0) const override { throw std::runtime_error{"no peeking"}; }

    void clear() override
    {
        while (!uops.empty())
            uops.pop();
    }

    void clear_if(const std::function<bool(const core::Uop&)>& pred) override
    {
        while (!uops.empty()) {
            if (!pred(make_uop(uops.front())))
                return;
            uops.pop();
        }
    }

    struct UopInfo
    {
        std::uint32_t opcode{0b1'0'0'100010'0'000000000000'00000'00000}; // ADD 64 by default

        unsigned partition{0};
        SchedulerType table{0};
        PipeType pipe{PipeType::INVALID};

        archx::LReg dst{archx::LReg::INVALID};
        archx::LReg dst2{archx::LReg::INVALID};
        archx::LReg src1{archx::LReg::INVALID};
        archx::LReg src2{archx::LReg::INVALID};
        archx::LReg src3{archx::LReg::INVALID};

        bool flag_producer{false};
    };

    std::regex uop_pat{
        "Sch([ABCDXY])"         // pipe name
        "([01])"                // partition number
        "(\\.[SF])?"            // optional ".S" suffix, meaning "set flags", or ".F", "consume flags"
        "(( +"                  // up to three register arguments ("_" means invalid) organized as:
        "(?:(?:reg[XV]\\d+)|_)" //     DEST SRC1 SRC2
        "){0,3})"
        "(?: */ *(regX\\d+))?" // optional second DEST
    };

    std::regex arg_pat{"reg[XV]\\d+|_"};

    archx::LReg convert_reg(const std::string& name)
    {
        if (name == "_")
            return archx::LReg::INVALID;
        else if (name[3] == 'X')
            return archx::LReg(int(archx::LReg::L0) + std::stoi(name.substr(4)));
        else
            return archx::LReg(int(archx::LReg::V0) + std::stoi(name.substr(4))); // name[3] == 'V'
    }

    void add_uop(const std::string& s)
    {
        UopInfo info;
        std::smatch matches;

        if (std::regex_match(s, matches, uop_pat)) {
            info.partition = std::stoi(matches[2]);
            auto pipe_name = matches[1].str()[0];
            auto sch_name = pipe_name == 'C' || pipe_name == 'D' ? 'M' : pipe_name;
            info.table = util::string_to_enum<SchedulerType>(std::string(1, sch_name));
            info.pipe = util::string_to_enum<PipeType>(std::string(1, pipe_name));

            if (matches[3].matched) {
                if (matches[3].str() == ".S") {
                    // FIXME: Seems to be some confusion about how flag producers are detected.
                    info.opcode = 0b1'0'0'100010'0'000000000000'00000'00000; // ADDS
                    info.flag_producer = true;
                } else {
                    // Any conditional branch should work as a flag consumer.
                    info.opcode = 0b0101010'0'0000000000000000000'0'0000; // BC_EQ
                    info.src3 = archx::LReg::FLAGS;
                }
            }

            if (matches[4].matched) {
                std::string args = matches[4];
                auto arg_iter = std::sregex_iterator(args.begin(), args.end(), arg_pat);
                auto arg_end = std::sregex_iterator();

                if (arg_iter != arg_end) {
                    info.dst = convert_reg(arg_iter->str());
                    ++arg_iter;
                }

                if (arg_iter != arg_end) {
                    info.src1 = convert_reg(arg_iter->str());
                    ++arg_iter;
                }

                if (arg_iter != arg_end) {
                    info.src2 = convert_reg(arg_iter->str());
                    ++arg_iter;
                }
            }

            if (matches[6].matched) {
                info.dst2 = convert_reg(matches[6].str());
            }

            uops.push(info);
        } else {
            throw std::runtime_error{std::string{"bad uop pattern: "} + s};
        }
    }

    core::Uop make_uop(const UopInfo& info)
    {
        std::shared_ptr<archx::Instruction> archx_instr = std::make_shared<archx::InstructionSimple>(4_pc, info.opcode);
        UopSet archx_uops = furiosa::decode(archx_instr->opcode());
        AMPERE_ASSERT(archx_uops.size() == 1, "unexpected decode result");

        auto archx_uop = std::move(archx_uops[0]);

        archx_uop->src1 = info.src1;
        archx_uop->src2 = info.src2;
        archx_uop->src3 = info.src3;
        archx_uop->dst = info.dst;

        if (info.flag_producer)
            archx_uop->produce_flags = true;

        auto fetch = trans_make<core::Fetch>(0_pc, 1);
        auto instr = trans_make<core::Instruction>(std::move(archx_instr), fetch);
        auto uop = trans_make<core::Uop>(std::move(archx_uop), std::move(instr));

        uop->alloc_id = alloc_counter++;
        uop->sch.partition = info.partition;
        uop->sch.pipe_type = info.pipe;
        uop->sch.sched_type = info.table;
        uop->reg_dst2 = info.dst2;

        for (uint s = 0; s < UOP_MAX_SRCS; s++)
            uop->reg_psrc[s] = spec_rf_table[uop->reg_src[s]];

        if (!archx::invalid_reg(info.dst)) {
            uop->prfid = rf_int.acquire();
            spec_rf_table[info.dst] = uop->prfid->get_id();
        }
        if (!archx::invalid_reg(info.dst2)) {
            uop->prfid2 = rf_int.acquire();
            spec_rf_table[info.dst2] = uop->prfid2->get_id();
        }
        if (info.flag_producer) {
            uop->flagsid = rf_flag.acquire();
            spec_rf_table[archx::LReg::FLAGS] = uop->flagsid->get_id();
        }

        return uop;
    }

    std::queue<UopInfo> uops;
    std::uint64_t alloc_counter{0};
    CountedCreditPool rf_int{"rf_int"}, rf_flag{"rf_flag"}, rf_fp{"rf_fp"};
    uint64_t spec_rf_table[util::enum_max_numeric_size<archx::LReg>()]{0};
};

class Top : public Module
{
public:
    Top(const sc_core::sc_module_name& name, Clock* clock, Environment* env) : Module(name, clock, env)
    {
        Scheduler::set_active_partition_counts(2, 1); // IXU_PARTITIONS = 2, fsu_partitions = 1
        std::vector<size_t> vec(NUM_SCHED, 24);
        ixu.init(vec);
        ixu.dispatch_port.bind(og);
        ixu.flush_port.bind(ixu_flush);
        ixu.pmu_stall_xu_busy.bind(pmu_stall_xu_busy_mock);
        ixu.pmu_stall_ilock.bind(pmu_stall_ilock_mock);
        ixu.commit_port.bind(commit_port);
        ixu.credit_return.bind(credit_port);
        ixu.load_cancel_in.bind(load_cancel_mock);

        for (int i = 0; i < ixu.num_execution_units; i++) {
            ixu.writeback_xu[i].bind(writeback_ports[i]);
            ixu.complete_xu[i].bind(complete_ports[i]);
            ixu.mem_issue_port[i].bind(mem_issue_ports[i]);
        }
    }

    IXU ixu{"ixu_partitioned"};
    AllocatedUopGenerator og;
    std::array<NeverTake<core::Uop>, NUM_EXECUTION_UNITS> writeback_ports;
    std::array<NeverTake<std::pair<core::Uop, bool>>, NUM_EXECUTION_UNITS> complete_ports;
    std::array<UopConsumer, NUM_EXECUTION_UNITS> mem_issue_ports;
    NiceMock<MockFlushReader> ixu_flush;
    NiceMock<MockPutter<std::vector<bool>>> pmu_stall_xu_busy_mock;
    NiceMock<MockPutter<std::vector<bool>>> pmu_stall_ilock_mock;
    UopConsumer commit_port;
    CreditReleaser credit_port;
    NiceMock<MockTaker<core::Uop>> load_cancel_mock;
};

TEST_F(IXUTest, testConstructor)
{
    Top top("top", &clock, &env);
}

TEST_F(IXUTest, CanScheduleOneUop)
{
    Top top("top", &clock, &env);

    top.og.add_uop("SchA0 regX0 regX1");
    clock.tick(4);
    EXPECT_EQ(top.commit_port.uops.size(), 1);
    top.commit_port.release();
}

TEST_F(IXUTest, IndependentUopsScheduleTogether)
{
    Top top("top", &clock, &env);

    top.og.add_uop("SchA0 regX0 regX1");
    top.og.add_uop("SchA1 regX2 regX3");
    clock.tick(4);
    EXPECT_EQ(top.commit_port.uops.size(), 2);
    top.commit_port.release();
}

TEST_F(IXUTest, DependentUopsAreSerialized)
{
    Top top("top", &clock, &env);

    top.og.add_uop("SchA0 regX0 regX1");
    top.og.add_uop("SchA1 regX2 regX0");
    clock.tick(4);
    EXPECT_EQ(top.commit_port.uops.size(), 1);
    clock.tick(4);
    EXPECT_EQ(top.commit_port.uops.size(), 2);
    top.commit_port.release();
}

TEST_F(IXUTest, FlagProducerAndConsumerAreSerialized)
{
    Top top("top", &clock, &env);

    top.og.add_uop("SchA0.S");
    top.og.add_uop("SchA1.F");
    clock.tick(4);
    EXPECT_EQ(top.commit_port.uops.size(), 1);
    clock.tick(4);
    EXPECT_EQ(top.commit_port.uops.size(), 2);
    top.commit_port.release();
}

TEST_F(IXUTest, IssueFourPipesAtOnce)
{
    Top top("top", &clock, &env);

    // Four uops will dispatch in a cycle. Since these are all independent, they should issue "back to back".
    //
    top.og.add_uop("SchA0 regX0 regX1");
    top.og.add_uop("SchA1 regX2 regX3");
    top.og.add_uop("SchB0 regX4 regX5");
    top.og.add_uop("SchB1 regX6 regX7");

    // These are on SchX/Y which take a cycle longer so they should not be
    // seen right away, but rather one cycle later
    //
    top.og.add_uop("SchX0 regX8 regX9");
    top.og.add_uop("SchY0 regX10 regX11");

    clock.tick(4);
    EXPECT_EQ(top.commit_port.uops.size(), 4);

    clock.tick(1);
    EXPECT_EQ(top.commit_port.uops.size(), 6);
    top.commit_port.release();
}

TEST_F(IXUTest, IntegerAndVectorRegsAreSeperate)
{
    env.parameters.add_setting("top.ixu_partitioned.fsu_dispatch_latency", 0, "test bench");
    Top top("top", &clock, &env);

    top.og.add_uop("SchA0 regX0 regX1");
    top.og.add_uop("SchX0 regV2 regV0");
    clock.tick(4);
    EXPECT_EQ(top.commit_port.uops.size(), 2);
    top.commit_port.release();
}

TEST_F(IXUTest, SchDependentOnSecondDestIsSerialized)
{
    Top top("top", &clock, &env);

    top.og.add_uop("SchA0 regX0 regX1 / regX2");
    top.og.add_uop("SchA1 regX2 regX2");
    clock.tick(4);
    EXPECT_EQ(top.commit_port.uops.size(), 1);
    clock.tick(4);
    EXPECT_EQ(top.commit_port.uops.size(), 2);
    top.commit_port.release();
}

TEST_F(IXUTest, SchDependentOnSecondDestSchedulesAfterFirstIsComplete)
{
    Top top("top", &clock, &env);

    // For reasons I don't understand, having uops waiting in the dispatch_port does not cause the the IXU run()
    // function to get called when running in the gtest environment. This test now ensures that all the uops it wants to
    // run are in the IXU before it goes idle -- if we wait for the first two to commit before adding the third, IXU
    // never runs.
    top.og.add_uop("SchA0 regX0 regX1 / regX2");
    top.og.add_uop("SchB0 regX1 regX3");
    clock.tick(1);
    top.og.add_uop("SchA1 regX2 regX2");
    clock.tick(3);
    EXPECT_EQ(top.commit_port.uops.size(), 2);
    clock.tick(4);
    EXPECT_EQ(top.commit_port.uops.size(), 3);
    top.commit_port.release();
}

} // namespace ampere

/*
 * vim: sw=4:ts=4:et:si:cindent:cino=\:0,(0,g0,t0:
 */
