//
// Copyright (c) 2021 Ampere Computing. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause
//

#ifndef AMPERE_CORE_CP_DIVIDE_CONQUER_H
#define AMPERE_CORE_CP_DIVIDE_CONQUER_H

#include "ampere/core/prefetcher.h"
#include "ampere/sc/sc.h"
#include "ampere/sc/submodule.h"

namespace ampere::core {

using namespace ampere::sc;

class CP_SN4L : public Prefetcher<Fetch>, implementation<CP_SN4L, Prefetcher<Fetch>>
{
public:
    explicit CP_SN4L(sc_core::sc_module_name name);
    bool run() override;

protected:
    void train_impl(Fetch req, bool hit, bool prefetch, bool demand) override;
    void evict_impl(PhysAddr pa, bool prefetch, bool demand) override;

private:
    Event ev_train{"train", "SN4L was trained"};
    Event ev_trigger{"trigger", "SN4L was triggered"};
    EventData<bool> ev_hitmiss{"train_by_hit", "SN4L trained by hit to a prefetched line"};
    EventData<bool> ev_prefetch{"train_by_prefetch", "SN4L trained by prefetch"};

    Param<unsigned> depth{"depth", "depth of sequential prefetcher", 4};
    Param<unsigned> seq_table_size{"seq_table_size", "size of seq table", 32768};
    Param<unsigned> rlu_table_size{"rlu_table_size", "size of RLU history", 12};
    Param<unsigned> rlu_max_qsize{"rlu_max_qsize", "max size of rlu queue", 12};

    struct Entry
    {
        Entry() = default;
        Entry(Fetch f, ProgramCounter vaddr, PhysAddr addr) : fetch(f), pc(vaddr), pa(addr) {}

        Fetch fetch;
        ProgramCounter pc{0};
        PhysAddr pa{0};
    };

    struct RLUHistory
    {
        explicit RLUHistory(unsigned size);
        void insert(Entry e);
        bool find(PhysAddr pa);

        std::vector<Entry> table;
    };

    void table_print(std::ostream &os) const;
    void table_print();
    void dump(std::ostream &os) const override;
    unsigned calculate_index(archx::PhysAddr pa);

    std::vector<std::pair<ProgramCounter, bool>> seq_table;
    std::deque<Entry> rlu_queue;
    unsigned pc_bits{0};
    RLUHistory *rlu{nullptr};
};

} // namespace ampere::core
#endif // AMPERE_CORE_CP_DIVIDE_CONQUER_H

/*
 * vim: sw=4:ts=4:et:si:cindent:cino=\:0,(0,g0,t0:
 */
