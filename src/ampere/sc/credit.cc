//
// Copyright (c) 2018-2020 Ampere Computing. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause
//
#include <boost/make_unique.hpp>
#include <iostream>
#include "ampere/sc/credit.h"
#include "ampere/sc/report.h"

namespace ampere::sc {

Credit CreditPool::acquire()
{
    AMPERE_ASSERT(count, "credit pool exhausted");

    --count;

    ev_acquire(ev_occupancy(get_occupancy()));

    return std::unique_ptr<CreditObj>{new CreditObj{this}};
}

void CreditPool::shrink(size_t shrink_count)
{
    if (frozen)
        AMPERE_ERROR("can't shrink credit pool after elaboration");
    AMPERE_ASSERT(shrink_count <= count, "can't shrink credit pool to less than zero");
    count -= shrink_count;
}

void CreditPool::release(CreditObj* c)
{
    AMPERE_ASSERT(c->pool == this, "released credit does not belong to pool");

    ev_release();
    c->pool = nullptr;
    ++count;
}

void CreditPool::end_of_elaboration()
{
    frozen = true;
}

CreditObj::~CreditObj()
{
    if (pool && !sc_core::sc_end_of_simulation_invoked() && !std::uncaught_exceptions()) {
        std::cerr << "credit " << id << " for " << pool->pool_name() << " was leaked" << std::endl;
        std::terminate();
    }
}

void CreditObj::release()
{
    pool->release(this);
}

void release_credit(Credit& credit)
{
    if (credit) {
        credit->release();
        credit.reset(nullptr);
    } else {
        AMPERE_FATAL("Attempt to release a credit twice");
    }
}

Credit CountedCreditPool::acquire()
{
    if (!count)
        AMPERE_ERROR("credit pool exhausted");
    AMPERE_ASSERT(freelist.size() == count, "Freelist size mismatch in acquire()");

    // Get a credit, assign it an ID from the freelist, and increment its reference count
    Credit c = this->CreditPool::acquire();
    c->id = freelist.back();
    freelist.pop_back();
    use_count[c->id - base_id]++;

    return c;
}

void CountedCreditPool::release(CreditObj* c)
{
    if (c->pool != static_cast<CreditPool*>(this))
        AMPERE_ERROR("released credit does not belong to pool");
    AMPERE_ASSERT(freelist.size() == count, "Freelist size mismatch in release()");

    ev_release();

    c->pool = nullptr;

    // Decrement the reference count of this credit's ID, and free it up if the count is 0
    if (--use_count[c->id - base_id] == 0) {
        freelist.push_back(c->id);
        ++count;
    }
}

void CountedCreditPool::set_base_id(uint64_t new_base)
{
    if (frozen)
        AMPERE_ERROR("can't change credit IDs after elaboration");
    if (freelist.size() != size)
        AMPERE_ERROR("freelist not full when changing credit IDs");

    base_id = new_base;
    for (uint64_t i = 0; i < size; i++)
        freelist[i] = base_id + i;
}

void CountedCreditPool::shrink(size_t shrink_count)
{
    if (frozen)
        AMPERE_ERROR("can't shrink credit pool after elaboration");
    AMPERE_ASSERT(shrink_count <= count, "can't shrink credit pool to less than zero");

    count -= shrink_count;
    for (uint64_t i = 0; i < shrink_count; i++)
        freelist.pop_back();
}

Credit CountedCreditPool::duplicate(uint64_t dup)
{
    AMPERE_ASSERT(dup >= base_id && dup < base_id + size, "Duplicated credit ID out of range");
    AMPERE_ASSERT(use_count[dup - base_id] != 0, "Duplicated credit ID is not already in use");

    // Return a credit which is a duplicate of the one with the requested ID, which increments
    // the reference count of that ID
    use_count[dup - base_id]++;
    auto credit = new CreditObj{this};
    credit->id = dup;
    return std::unique_ptr<CreditObj>{credit};
}

} // namespace ampere::sc

/*
 * vim: sw=4:ts=4:et:si:cindent:cino=\:0,(0,g0,t0:
 */
