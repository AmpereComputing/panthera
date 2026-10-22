//
// Copyright (c) 2019-2020 Ampere Computing. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause
//

#pragma once

#include <cstdint>
#include <optional>
#include <vector>

#include "ampere/sc/report.h"
#include "ampere/archx/addr.h"
#include "ampere/archx/arch_state.h"
#include "ampere/archx/bitfield.h"
#include "ampere/archx/driver.h"
#include "ampere/archx/page_size.h"
#include "ampere/archx/sysreg.h"
#include "ampere/archx/translation.h"
#include "ampere/uarch_common/cache.h"

namespace ampere::core {

using archx::Bitfield;
using archx::PageSize;
using archx::PhysAddr;
using archx::Translation;
using archx::VirtAddr;

template<typename VA = VirtAddr>
class TLBTag : public VA
{
public:
    TLBTag(VA addr, uint16_t asid) : VA(addr), asid(asid) {}
    using addr_t = VA;
    const uint16_t asid;
};

template<typename PA = PhysAddr>
class TLBEntry
{
public:
    TLBEntry(const PA& pa, const PageSize& ps) : phys_addr{pa}, page_size{ps} {}

    PhysAddr phys_addr{};
    PageSize page_size{};

    template<typename VA>
    auto line_address(VA address) const
    {
        auto page_base = phys_addr.page_address(page_size).value();
        auto untranslated_bits = address.extract_range(page_size.page_bits() - 1, 0);
        return PhysAddr{page_base | untranslated_bits}.line_address().value();
    }
};

enum class TLBType {
    DATA,
    INSTRUCTION,
    BOTH,
};

template<typename Tag, typename Entry, TLBType Type = TLBType::BOTH>
class TLB : public uarch_common::ManagedCache<Tag, Entry, Tag>
{
    using Base = uarch_common::ManagedCache<Tag, Entry, Tag>;
    using CacheResult = uarch_common::CacheResult<Entry>;

public:
    archx::DriverPortOpt driver;
    sc::Param<bool> enable_perfect_mode_user{"enable_perfect_mode_user", "Force all user-mode lookups to hit", false};

    using Address = typename Tag::addr_t;

    explicit TLB(const sc_core::sc_module_name& name, Entry perfect_value = {})
        : Base(name, perfect_value), perfect_result_entry(std::move(perfect_value))
    {
    }

    // FIXME: we don't actually use the ASID or ttl values, only the VA. This will over-invalidate in some cases.
    std::vector<Entry> invalidate_by_va(uint16_t asid, uint8_t ttl, Address va)
    {
        std::vector<Entry> victims;

        // When invalidating by VA we can't just do find_entry() because we don't want to match on ASID, is_instr, etc.
        // For simplicity we just look at all entries -- TLBI is rare so it doesn't matter that this is inefficient.
        for (auto& set : this->get_sets()) {
            for (auto way_iter = set.begin(); way_iter != set.end();) {
                auto& tag = way_iter->first;
                auto& entry = way_iter->second;

                if (va.same_page(tag, entry.value.page_size)) {
                    victims.emplace_back(std::move(entry.value));
                    auto index = this->compute_set(tag);
                    this->crp->invalidate(index, entry.way);
                    way_iter = set.erase(way_iter);
                } else {
                    way_iter++;
                }
            }
        }

        return victims;
    }

    static inline constexpr bool is_instr(const Tag& tag [[maybe_unused]])
    {
        if constexpr (Type == TLBType::DATA) {
            return false;
        } else if constexpr (Type == TLBType::INSTRUCTION) {
            return true;
        } else {
            return tag.instr();
        }
    }

    CacheResult lookup(sc::Txn transaction,
                       Tag address,
                       bool update_lru = true,
                       bool suppress_event = false,
                       bool prefetched = false,
                       uint64_t event_addr = 0x0)
    {
        AMPERE_ASSERT(!this->perfect_hit(address) || driver_connected, "Driver unconnected while in perfect mode");
        if (auto result = Base::lookup(transaction, address, update_lru, suppress_event, prefetched, event_addr);
            !this->perfect_hit(address) || (result && result->phys_addr != this->perfect_value->value.phys_addr))
            return result;

        if (auto result = translate(address); result) {
            perfect_result_entry = *result;
            return CacheResult{perfect_result_entry};
        }

        return std::nullopt;
    }

    CacheResult lookup(sc::Txn transaction,
                       const archx::ArchState& state,
                       Tag address,
                       bool update_lru = true,
                       bool suppress_event = false,
                       bool prefetched = false,
                       uint64_t event_addr = 0x0)
    {
        if (auto result = Base::lookup(transaction, address, update_lru, suppress_event, prefetched, event_addr);
            !this->perfect_hit(address) || (result && result->phys_addr != this->perfect_value->value.phys_addr))
            return result;

        if (auto result = translate(state, address); result) {
            perfect_result_entry = *result;
            return CacheResult{perfect_result_entry};
        }

        return std::nullopt;
    }

    bool perfect_hit(Tag address) const override
    {
        // VA[52] is between the upper end of the VA space (even with 64k pages, which allow 52-bit VAs)
        // and below any bits affected by TBI/MTE, so it's the reliable bit to look at to determine
        // whether an address goes through TTBR0 or TTBR1.
        return Base::perfect_hit(address) || (enable_perfect_mode_user && address[52] == 0);
    }

    void dump(std::ostream& os = std::cout) const override
    {
        fmt::print(os, "\n===== {} contents =====\n", this->name());
        for (uint64_t s = 0; s < this->num_sets; s++) {
            auto set = this->get_set(s);

            for (const auto& [tag, entry] : set) {
                fmt::print(os,
                           "set {} way {}: VA {:#x} asid {} -> PA {:#x} page size {}\n",
                           s,
                           entry.way,
                           tag.value() & ~util::mask(entry.value.page_size.page_bits()),
                           tag.asid,
                           entry.value.phys_addr.value() & ~util::mask(entry.value.page_size.page_bits()),
                           (std::string)entry.value.page_size);
            }
        }
    }

protected:
    uarch_common::CacheEntry::Way<Entry> perfect_result_entry;
    bool driver_connected = false;

    void end_of_elaboration() override { driver_connected = driver.bind_count(); }

    uarch_common::Index compute_set(Tag tag) const override
    {
        if (this->set_bits == 0)
            return 0;
        auto index = tag.extract_range(this->line_bits + this->set_bits - 1, this->line_bits);
        if (index >= this->num_sets)
            throw std::logic_error("set index is larger than the number of sets");
        return index;
    }

    Tag compute_tag(Tag tag) const override { return tag; }

    std::optional<Entry> translation_to_entry(const Translation& tr, Tag address) const
    {
        if (tr.faulted())
            return std::nullopt;

        auto [phys_addr, page_size] = tr.result();
        if constexpr (Type == TLBType::BOTH) {
            return Entry{phys_addr, page_size, is_instr(address)};
        } else {
            return Entry{phys_addr, page_size};
        }
    }

    std::optional<Entry> translate(Tag address) const
    {
        Translation tr(VirtAddr{address.value()},
                       Bitfield{driver->read_sysreg(archx::TCR_EL1)},
                       Bitfield{driver->read_sysreg(archx::TTBR0_EL1)},
                       Bitfield{driver->read_sysreg(archx::TTBR1_EL1)},
                       is_instr(address));

        while (!tr.done()) {
            PhysAddr pte_addr = tr.next_pte();
            uint64_t pte_contents = driver->read_memory(pte_addr);
            tr.pte_fetched(pte_contents);
        }

        return translation_to_entry(tr, address);
    }

    std::optional<Entry> translate(const archx::ArchState& state, Tag address) const
    {
        Translation tr(VirtAddr{address.value()},
                       Bitfield{state.read(archx::TCR_EL1)},
                       Bitfield{state.read(archx::TTBR0_EL1)},
                       Bitfield{state.read(archx::TTBR1_EL1)},
                       is_instr(address));

        while (!tr.done()) {
            auto pte_addr = tr.next_pte();
            auto pte_contents = state.read(pte_addr);
            tr.pte_fetched(pte_contents);
        }

        return translation_to_entry(tr, address);
    }
};

} // namespace ampere::core

/*
 * vim: sw=4:ts=4:et:si:cindent:cino=\:0,(0,g0,t0:
 */
