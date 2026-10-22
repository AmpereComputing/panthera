//
// Copyright (c) 2018-2020 Ampere Computing. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause
//
#pragma once

#include "ampere/sc/module.h"
#include "ampere/sc/param.h"
#include "ampere/sc/report.h"

namespace ampere::sc {

class CreditPoolIfc;

class CreditObj
{
public:
    virtual ~CreditObj();
    void release();
    uint64_t get_id() const { return id; }

private:
    explicit CreditObj(CreditPoolIfc* pool, uint64_t id = 0) : pool{pool}, id{id} {}

    CreditPoolIfc* pool;
    uint64_t id;

    friend class CreditPool;
    friend class CountedCreditPool;
    friend class SimpleCreditPool;
};

using Credit = std::unique_ptr<CreditObj>;

class CreditPoolIfc
{
public:
    virtual ~CreditPoolIfc() = default;

    virtual bool can_acquire(size_t need = 1) const = 0;
    virtual Credit acquire() = 0;
    virtual void release(CreditObj*) = 0;

    virtual size_t get_size() const = 0;
    virtual size_t get_count() const = 0;

    virtual std::string pool_name() const { return "unknown"; }

    void release(Credit c) { release(c.get()); }
    size_t get_occupancy() const { return get_size() - get_count(); };

    bool all_free() const { return get_size() == get_count(); }
};

void release_credit(Credit& credit);

class CreditPool : public Module, public CreditPoolIfc
{
public:
    Param<size_t> size{"size", "size of the credit pool (resource count)", 0};

    struct explicit_size
    {};
    CreditPool(explicit_size, const sc_core::sc_module_name& name, size_t capacity)
        : Module(name), size{"size", "size of the credit pool (resource count)", capacity}
    {
        count = size;
    }
    CreditPool(const sc_core::sc_module_name& name) : Module(name) { count = size; }

    ~CreditPool() override = default;

    size_t get_count() const override { return count; }
    size_t get_size() const override { return size; }
    bool can_acquire(size_t need = 1) const override
    {
        AMPERE_ASSERT(size > 0,
                      "attempt to acquire credit from empty pool {} (probably a missing \"size\" configuration)",
                      name());
        if (count == 0)
            ((CreditPool*)this)->ev_out_of_credits();
        return count >= need;
    }
    Credit acquire() override;
    void release(CreditObj*) override;
    std::string pool_name() const override { return name(); }

    virtual void shrink(size_t size);
    void end_of_elaboration() override;

protected:
    Event ev_acquire{"acquire", "Acquire credit"};
    Event ev_release{"release", "Release credit"};
    Event ev_out_of_credits{"out_of_credits", "Pool was out of credits when can_acquire() was called"};
    EventData<int> ev_occupancy{"occupancy", "Credit pool occupancy"};

    size_t count;
    bool frozen{false};

    friend class CreditObj;
};

class CountedCreditPool : public CreditPool
{
    // use this when you care about the specific ids in the credits
public:
    explicit CountedCreditPool(const sc_core::sc_module_name& name) : CreditPool{name}, use_count(size)
    {
        for (uint64_t i = 0; i < size; i++) {
            freelist.push_back(i);
            use_count[i] = 0;
        }
    }

    Credit acquire() override;
    void release(CreditObj*) override;
    void set_base_id(uint64_t base);
    void shrink(size_t size) override;
    Credit duplicate(uint64_t);

private:
    std::vector<uint64_t> freelist;
    std::vector<uint64_t> use_count;
    uint64_t base_id{0};
};

class SimpleCreditPool : public CreditPoolIfc
{
public:
    SimpleCreditPool(const std::string& name, size_t size) : name{name}, size{size}, count{size} {}

    ~SimpleCreditPool() override = default;

    bool can_acquire(size_t need = 1) const override { return count >= need; }

    Credit acquire() override
    {
        AMPERE_ASSERT(count, "credit pool exhausted");
        --count;
        auto credit = new CreditObj{this};
        return std::unique_ptr<CreditObj>{credit};
    }

    void release(CreditObj* c) override
    {
        AMPERE_ASSERT(c->pool == this, "released credit does not belong to pool {}", pool_name());
        c->pool = nullptr;
        ++count;
    }

    size_t get_size() const override { return size; }
    size_t get_count() const override { return count; }

    std::string pool_name() const override { return name; }

private:
    std::string name;
    size_t size;
    size_t count;
};

} // namespace ampere::sc

/*
 * vim: sw=4:ts=4:et:si:cindent:cino=\:0,(0,g0,t0:
 */
