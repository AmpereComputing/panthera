//
// Copyright (c) 2018-2019 Ampere Computing. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause
//

#ifndef AMPERE_CORE_COHERENCY_VALIDATOR_H
#define AMPERE_CORE_COHERENCY_VALIDATOR_H

#include "ampere/sc/sc.h"

namespace ampere {
namespace core {

template<typename Address>
class CoherencyValidationInterface : public sc_core::sc_interface
{
public:
    virtual bool line_present(const Address&, bool iside) = 0;
};

template<typename Address>
using CoherencyValidator = sc_core::sc_port<CoherencyValidationInterface<Address>>;

} // namespace core
} // namespace ampere

#endif // AMPERE_CORE_COHERENCY_VALIDATOR_H

/*
 * vim: sw=4:ts=4:et:si:cindent:cino=\:0,(0,g0,t0:
 */
