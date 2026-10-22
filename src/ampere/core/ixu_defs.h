//
// Copyright (c) 2019-2022 Ampere Computing. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause
//
#pragma once

#include <algorithm>
#include <cstdint>
#include <optional>
#include <string>
#include <set>
#include <type_traits>

#include "ampere/archx/uop_info.h"
#include "ampere/sc/report.h"
#include "ampere/util/enum.h"

make_ns_enum_without_class_with_type(ampere::core::Sch, SchedulerType, unsigned,
    A,
    B,
    J,
    K,
    M,
    N,
    X,
    Y
);

make_ns_enum_without_class(ampere::core::Pipe, PipeType,
    A,
    B,
    J,
    K,
    S,
    C,
    D,
    E,
    F,
    X,
    Y,
    Z,
    U,
    V,
    W,
    BS,
    INVALID
);

namespace ampere::core {

using SchedulerType = Sch::SchedulerType;
using PipeType = Pipe::PipeType;

static constexpr auto IXU_SCH_START = Sch::A;
static constexpr auto MEM_SCH_START = Sch::M;
static constexpr auto FSU_SCH_START = Sch::X;

static constexpr auto IXU_SCH_END = MEM_SCH_START;
static constexpr auto MEM_SCH_END = FSU_SCH_START;
static constexpr auto FSU_SCH_END = util::enum_size<SchedulerType>();

static constexpr unsigned IXU_SCH_SIZE = MEM_SCH_END - IXU_SCH_START;
static constexpr unsigned IXU_INT_SCH_SIZE = IXU_SCH_END - IXU_SCH_START;
static constexpr unsigned MEM_SCH_SIZE = MEM_SCH_END - MEM_SCH_START;
static constexpr unsigned FSU_SCH_SIZE = FSU_SCH_END - FSU_SCH_START;

// MAXIMUM number of partitions allocated by type:
static constexpr unsigned IXU_PARTITIONS = 2;
static constexpr unsigned MAX_FSU_PARTITIONS = 2;
static constexpr unsigned MAX_PARTITIONS = std::max(IXU_PARTITIONS, MAX_FSU_PARTITIONS);

static constexpr unsigned TOTAL_IXU_SCHED = IXU_SCH_SIZE * IXU_PARTITIONS;
static constexpr unsigned TOTAL_FSU_SCHED = FSU_SCH_SIZE * MAX_FSU_PARTITIONS;

static constexpr unsigned NUM_SCHED = TOTAL_IXU_SCHED + TOTAL_FSU_SCHED;

inline static unsigned normalized_to(unsigned partition, unsigned partitions)
{
    return std::min(partition, partitions - 1);
}

inline static bool is_int_sch(SchedulerType sch)
{
    return sch >= IXU_SCH_START && sch < IXU_SCH_END;
}

inline static bool is_mem_sch(SchedulerType sch)
{
    return sch >= MEM_SCH_START && sch < MEM_SCH_END;
}

inline static bool is_fsu_sch(SchedulerType sch)
{
    return sch >= FSU_SCH_START && sch < FSU_SCH_END;
}

inline static bool is_ixu_op(SchedulerType op)
{
    return op >= IXU_SCH_START && op < MEM_SCH_END;
}
inline static bool is_fsu_op(SchedulerType op)
{
    return op >= FSU_SCH_START && op < FSU_SCH_END;
}

class Scheduler
{
public:
    std::optional<SchedulerType> sched_type;
    unsigned partition = 0;

    // IDR gets to decide how many active partitions there are, which is sent here by core.
    static inline unsigned ixu_partitions;
    static inline unsigned fsu_partitions;
    static void set_active_partition_counts(unsigned ixu, unsigned fsu)
    {
        AMPERE_ASSERT(ixu <= IXU_PARTITIONS, "Too many IXU partitions ({} > {}", ixu, IXU_PARTITIONS);
        AMPERE_ASSERT(fsu <= IXU_PARTITIONS, "Too many FSU partitions ({} > {}", fsu, MAX_FSU_PARTITIONS);
        ixu_partitions = ixu;
        fsu_partitions = fsu;
    }
    static inline bool u_pipe_enable, v_pipe_enable, w_pipe_enable;
    static inline bool x_pipe_by_enable, y_pipe_by_enable;
    static inline bool loads_on_e_pipe, uvw_load_port_share;
    static inline int fsu_load_ports_per_ixu, uvw_pipes_per_fsu;

    static void set_fsu_by_enables(bool u, bool v, bool w, bool x, bool y, bool load_e, bool port_share)
    {
        AMPERE_ASSERT(u | w | x | y, "No pipe is enabled for 2 source fsu bypass operations");
        AMPERE_ASSERT(!x | !u, "X scheduler bypass operations cannot go to both X {} and U {}", x, u);
        AMPERE_ASSERT(!y | !v, "Y scheduler bypass operations cannot go to both Y {} and V {}", v, y);
        AMPERE_ASSERT(!y | !w, "Y scheduler bypass operations cannot go to both Y {} and W {}", y, w);
        AMPERE_ASSERT(!v | !w, "Y scheduler bypass operations cannot go to both V {} and W {}", v, w);
        u_pipe_enable = u;
        v_pipe_enable = v;
        w_pipe_enable = w;
        x_pipe_by_enable = x;
        y_pipe_by_enable = y;
        loads_on_e_pipe = load_e;
        uvw_load_port_share = port_share;

        fsu_load_ports_per_ixu = (1 + (load_e ? 1 : 0));
        uvw_pipes_per_fsu = ((u ? 1 : 0) + (v ? 1 : 0) + (w ? 1 : 0));
        AMPERE_ASSERT(
            (!uvw_load_port_share) || (fsu_load_ports_per_ixu * ixu_partitions >= uvw_pipes_per_fsu * fsu_partitions),
            "Not enough load ports {} to share with {} U/V/W pipes",
            fsu_load_ports_per_ixu * ixu_partitions,
            uvw_pipes_per_fsu * fsu_partitions);
    }

    static int fsu_load_port_for_uvw(PipeType pipe_type, int fsu_partition)
    {
        // If only one of u/v/w, then counts up by partition: U0, U1, U2
        // If 2 are enabled, goes U0, W0, U1, W1
        return fsu_partitions * uvw_pipes_per_fsu * fsu_partition + ((u_pipe_enable && (pipe_type != Pipe::U)) ? 1 : 0);
    }
    static int fsu_load_port_for_load(PipeType pipe_type, int ixu_partition)
    {
        // If not loads_on_e_pipe, then counts up by partition: C0, C1
        // With loads_on_e_pipe, counts C0,E0,C1,E1
        return ixu_partition * fsu_load_ports_per_ixu + ((pipe_type == PipeType::E) ? 1 : 0);
    }

    Scheduler() : sched_type(std::nullopt), partition(0) {}
    Scheduler(SchedulerType op, unsigned partition, bool normalize = false) : sched_type(op), partition(partition)
    {
        if (normalize)
            this->normalize();
        else
            check_normalized();
    }
    inline static Scheduler normalized(SchedulerType op, unsigned partition) { return {op, partition, true}; }

    inline static Scheduler from_sched(std::optional<unsigned> sched_type)
    {
        if (!sched_type || sched_type.value() >= NUM_SCHED)
            return Scheduler{};

        auto sched = sched_type.value();
        if (sched < TOTAL_IXU_SCHED)
            return {static_cast<SchedulerType>(sched % IXU_SCH_SIZE), sched / IXU_SCH_SIZE, true};

        sched -= TOTAL_IXU_SCHED;
        return {static_cast<SchedulerType>(FSU_SCH_START + sched % FSU_SCH_SIZE), sched / FSU_SCH_SIZE, true};
    }

    bool operator==(const Scheduler& other) const
    {
        return sched_type == other.sched_type && partition == other.partition;
    }

    bool is_ixu() const { return sched_type && is_ixu_op(*sched_type); }
    bool is_fsu() const { return sched_type && is_fsu_op(*sched_type); }

    unsigned scheduler() const
    {
        // FIXME: mapping picked for backwards compatibility (since RTL uses it too)
        // not necessarily scalable...
        if (is_ixu())
            return *sched_type + IXU_SCH_SIZE * normalized_partition();
        if (is_fsu())
            return TOTAL_IXU_SCHED + (*sched_type - FSU_SCH_START) + FSU_SCH_SIZE * normalized_partition();
        return NUM_SCHED;
    }

    uint64_t scheduler_vector() const { return 1ULL << scheduler(); }

    void reset_sched_type() { sched_type.reset(); }
    void check_normalized()
    {
        AMPERE_ASSERT(
            partition == normalized_partition(), "Invalid partition: {} != {}", partition, normalized_partition());
    }
    void normalize() { partition = normalized_partition(); }
    unsigned partition_normalized_to(unsigned partitions) const { return normalized_to(partition, partitions); }

    // Returns a name like "<scheduler>(<partition>)?"
    [[maybe_unused]] std::string name() const
    {
        if (!sched_type || sched_type >= NUM_SCHED)
            return "UNKN";

        auto op_name = util::enum_to_string(*sched_type);
        if ((is_ixu() && IXU_PARTITIONS == 1) || (is_fsu() && MAX_FSU_PARTITIONS == 1))
            return std::string(op_name);

        return std::string(op_name) + std::to_string(partition);
    }

    // Returns a name like "Sch<scheduler>(_P<partition>)?"
    [[maybe_unused]] std::string extended_name() const
    {
        if (!sched_type || sched_type >= NUM_SCHED)
            return "UNKN";

        auto op_name = util::enum_to_string(*sched_type);
        if ((is_ixu() && IXU_PARTITIONS == 1) || (is_fsu() && MAX_FSU_PARTITIONS == 1))
            return std::string(op_name);

        return std::string(op_name) + "_P" + std::to_string(partition);
    }

    static inline std::set<SchedulerType> eligible_scheduler_types(archx::Uoppipe allowed_pipes)
    {
        std::set<SchedulerType> schedulers;

        if (allowed_pipes & archx::Uoppipe::A)
            schedulers.emplace(Sch::A);
        if (allowed_pipes & archx::Uoppipe::B || allowed_pipes & archx::Uoppipe::BS
            || allowed_pipes & archx::Uoppipe::S)
            schedulers.emplace(Sch::B);
        if (allowed_pipes & archx::Uoppipe::J)
            schedulers.emplace(Sch::J);
        if (allowed_pipes & archx::Uoppipe::K)
            schedulers.emplace(Sch::K);

        if (allowed_pipes & archx::Uoppipe::C || allowed_pipes & archx::Uoppipe::D)
            schedulers.emplace(Sch::M);
        if (allowed_pipes & archx::Uoppipe::E || allowed_pipes & archx::Uoppipe::F)
            schedulers.emplace(Sch::N);

        if (allowed_pipes & archx::Uoppipe::Z) {
            schedulers.emplace(Sch::X);
            schedulers.emplace(Sch::Y);
        }

        if (allowed_pipes & archx::Uoppipe::X || allowed_pipes & archx::Uoppipe::U)
            schedulers.emplace(Sch::X);
        if (allowed_pipes & archx::Uoppipe::Y || allowed_pipes & archx::Uoppipe::V || allowed_pipes & archx::Uoppipe::W)
            schedulers.emplace(Sch::Y);

        return schedulers;
    }

    static inline std::vector<Scheduler> eligible_schedulers(archx::Uoppipe allowed_pipes)
    {
        auto allowed_schedulers = eligible_scheduler_types(allowed_pipes);
        std::vector<Scheduler> schedulers;
        schedulers.reserve(allowed_schedulers.size() * MAX_PARTITIONS);
        for (auto sch : allowed_schedulers) {
            unsigned partitions = is_fsu_op(sch) ? fsu_partitions : ixu_partitions;
            for (unsigned i = 0; i < partitions; ++i)
                schedulers.emplace_back(sch, i);
        }

        return schedulers;
    }

    unsigned normalized_partition() const
    {
        // Normalizes partition to actual range as it is somewhat overloaded with other usages
        // (like counter/scheduling balancing)
        if (is_ixu())
            return partition_normalized_to(ixu_partitions);
        if (is_fsu())
            return partition_normalized_to(fsu_partitions);
        return partition;
    }
};

static inline auto scheduler_enum()
{
    auto builder = util::FrozenEnumeration<std::underlying_type_t<SchedulerType>, NUM_SCHED>::create();
    for (unsigned p = 0; p < IXU_PARTITIONS; ++p) {
        for (unsigned i = IXU_SCH_START; i < FSU_SCH_START; ++i) {
            auto s = Scheduler(static_cast<SchedulerType>(i), p);
            builder.add(s.extended_name(), s.scheduler());
        }
    }
    for (unsigned p = 0; p < MAX_FSU_PARTITIONS; ++p) {
        for (unsigned i = FSU_SCH_START; i < FSU_SCH_END; ++i) {
            auto s = Scheduler(static_cast<SchedulerType>(i), p);
            builder.add(s.extended_name(), s.scheduler());
        }
    }

    return builder.freeze();
}

static inline unsigned scheduler_partitions(SchedulerType sched)
{
    switch (sched) {
    case Sch::A:
    case Sch::B:
    case Sch::J:
    case Sch::K:
    case Sch::M:
    case Sch::N:
        return Scheduler::ixu_partitions;
    case Sch::X:
    case Sch::Y:
        return Scheduler::fsu_partitions;
    default:
        AMPERE_ERROR("Unknown number of partitions for scheduler {}", sched);
    }

    return 1;
}

static constexpr auto IXU_PIPE_TYPE_START = Pipe::A;
static constexpr auto MEM_PIPE_TYPE_START = Pipe::C;
static constexpr auto FSU_PIPE_TYPE_START = Pipe::X;
static constexpr auto UNPARTITIONED_PIPE_TYPE_START = Pipe::BS;

static constexpr auto IXU_ALU_PIPE_TYPE_FIRST = Pipe::A;
static constexpr auto IXU_ALU_PIPE_TYPE_LAST = Pipe::S;
static constexpr auto FSU_ALU_PIPE_TYPE_FIRST = Pipe::X;
static constexpr auto FSU_ALU_PIPE_TYPE_LAST = Pipe::Y;

static constexpr auto IXU_PIPE_TYPE_END = MEM_PIPE_TYPE_START;
static constexpr auto MEM_PIPE_TYPE_END = FSU_PIPE_TYPE_START;
static constexpr auto FSU_PIPE_TYPE_END = UNPARTITIONED_PIPE_TYPE_START;
static constexpr auto UNPARTITIONED_PIPE_TYPE_END = Pipe::INVALID;

static constexpr unsigned ALU_PIPES = MEM_PIPE_TYPE_START - IXU_PIPE_TYPE_START;
static constexpr unsigned IXU_PIPES = FSU_PIPE_TYPE_START - IXU_PIPE_TYPE_START;
static constexpr unsigned MEM_PIPES = FSU_PIPE_TYPE_START - MEM_PIPE_TYPE_START;
static constexpr unsigned FSU_PIPES = UNPARTITIONED_PIPE_TYPE_START - FSU_PIPE_TYPE_START;
static constexpr unsigned UNPARTITIONED_PIPES = Pipe::INVALID - UNPARTITIONED_PIPE_TYPE_START;

static constexpr unsigned TOTAL_IXU_PIPES = IXU_PIPES * IXU_PARTITIONS;
static constexpr unsigned TOTAL_FSU_PIPES = FSU_PIPES * MAX_FSU_PARTITIONS;
static constexpr unsigned TOTAL_PIPES = TOTAL_IXU_PIPES + TOTAL_FSU_PIPES + UNPARTITIONED_PIPES;

inline static bool is_ixu_pipe(PipeType pipe)
{
    switch (pipe) {
    case Pipe::A:
    case Pipe::B:
    case Pipe::J:
    case Pipe::K:
    case Pipe::S:
    case Pipe::C:
    case Pipe::D:
    case Pipe::E:
    case Pipe::F:
    case Pipe::BS:
        return true;
    default:
        return false;
    }
}

inline static bool is_mem_pipe(PipeType pipe)
{
    switch (pipe) {
    case Pipe::C:
    case Pipe::D:
    case Pipe::E:
    case Pipe::F:
        return true;
    default:
        return false;
    }
}

inline static bool is_fsu_pipe(PipeType pipe)
{
    switch (pipe) {
    case Pipe::X:
    case Pipe::Y:
    case Pipe::Z:
    case Pipe::U:
    case Pipe::V:
    case Pipe::W:
        return true;
    default:
        return false;
    }
}

inline static PipeType corresponding_load_pipe(PipeType store_pipe)
{
    switch (store_pipe) {
    case Pipe::D:
        return Pipe::C;
    case Pipe::F:
        return Pipe::E;
    default:
        AMPERE_ERROR("Unexpected store pipe {}", store_pipe);
        return Pipe::INVALID;
    }
}

inline static bool is_pipe_partitioned(PipeType pipe)
{
    return static_cast<unsigned>(pipe) < UNPARTITIONED_PIPE_TYPE_START
           && ((is_ixu_pipe(pipe) && IXU_PARTITIONS > 1) || (is_fsu_pipe(pipe) && MAX_FSU_PARTITIONS > 1));
}

inline static unsigned expand_pipe(PipeType pipe, unsigned partition = 0)
{
    // Correct a port number if it is going to the wrong partition (i.e. i2f uop that must change partition)
    if (is_fsu_pipe(pipe))
        partition = normalized_to(partition, Scheduler::fsu_partitions);

    auto port = static_cast<unsigned>(pipe);
    if (port < FSU_PIPE_TYPE_START)
        return port + IXU_PIPES * std::min(partition, IXU_PARTITIONS - 1);
    if (port < UNPARTITIONED_PIPE_TYPE_START)
        return TOTAL_IXU_PIPES + (port - FSU_PIPE_TYPE_START) + FSU_PIPES * std::min(partition, MAX_FSU_PARTITIONS - 1);
    return TOTAL_IXU_PIPES + TOTAL_FSU_PIPES + (port - UNPARTITIONED_PIPE_TYPE_START);
}

inline static std::pair<PipeType, unsigned> split_pipe(unsigned port)
{
    if (port < TOTAL_IXU_PIPES)
        return {static_cast<PipeType>(port % IXU_PIPES), port / IXU_PIPES};

    if (port < TOTAL_IXU_PIPES + TOTAL_FSU_PIPES) {
        port -= TOTAL_IXU_PIPES;
        return {static_cast<PipeType>(FSU_PIPE_TYPE_START + port % FSU_PIPES), port / FSU_PIPES};
    }

    return {static_cast<PipeType>(port - TOTAL_IXU_PIPES - TOTAL_FSU_PIPES + UNPARTITIONED_PIPE_TYPE_START), 0};
}

[[maybe_unused]] inline static std::string pipe_name(int num)
{
    auto [pipe, partition] = split_pipe(num);
    if (num < 0 || pipe >= Pipe::INVALID)
        return "UNKN";

    auto pipe_name = util::enum_to_string(pipe);
    if (is_pipe_partitioned(pipe))
        return std::string(pipe_name) + "_P" + std::to_string(partition);
    return std::string(pipe_name);
}

static inline PipeType pipe_from_sch(SchedulerType scheduler, archx::Uoppipe pipe)
{
    switch (scheduler) {
    case Sch::A:
        return Pipe::A;
    case Sch::B:
        return Pipe::B;
    case Sch::J:
        return Pipe::J;
    case Sch::K:
        return Pipe::K;
    case Sch::M:
        if (pipe & archx::Uoppipe::C)
            return Pipe::C;
        else if (pipe & archx::Uoppipe::D)
            return Pipe::D;
        break;
    case Sch::N:
        if (pipe & archx::Uoppipe::E)
            return Pipe::E;
        else if (pipe & archx::Uoppipe::F)
            return Pipe::F;
        break;
    case Sch::X:
        if (pipe & archx::Uoppipe::Z)
            return Pipe::Z;
        if (pipe & archx::Uoppipe::U && Scheduler::u_pipe_enable)
            return Pipe::U;
        return Pipe::X;
    case Sch::Y:
        if (pipe & archx::Uoppipe::Z)
            return Pipe::Z;
        if (pipe & archx::Uoppipe::V && Scheduler::v_pipe_enable)
            return Pipe::V;
        if (pipe & archx::Uoppipe::W && Scheduler::w_pipe_enable)
            return Pipe::W;
        return Pipe::Y;
    }

    return Pipe::INVALID;
}

static inline auto pipe_enum()
{
    auto builder = util::FrozenEnumeration<std::underlying_type_t<PipeType>, TOTAL_PIPES>::create();
    for (unsigned p = 0; p < IXU_PARTITIONS; ++p) {
        for (unsigned i = IXU_PIPE_TYPE_START; i < FSU_PIPE_TYPE_START; ++i) {
            auto pipe = expand_pipe(static_cast<PipeType>(i), p);
            builder.add(pipe_name(pipe), pipe);
        }
    }
    for (unsigned p = 0; p < MAX_FSU_PARTITIONS; ++p) {
        for (unsigned i = FSU_PIPE_TYPE_START; i < FSU_PIPE_TYPE_END; ++i) {
            auto pipe = expand_pipe(static_cast<PipeType>(i), p);
            builder.add(pipe_name(pipe), pipe);
        }
    }
    for (unsigned i = UNPARTITIONED_PIPE_TYPE_START; i < UNPARTITIONED_PIPE_TYPE_END; ++i) {
        auto pipe = expand_pipe(static_cast<PipeType>(i), 0);
        builder.add(pipe_name(pipe), pipe);
    }

    return builder.freeze();
}

// This needs to be big enough to contain the sum of the integer,
// vector, flag, and predicate register files.
static const int MAX_PRF_SIZE = 2048;

} // namespace ampere::core
