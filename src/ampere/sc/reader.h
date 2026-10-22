//
// Copyright (c) 2019 Ampere Computing. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause
//

#ifndef PANTHERA_READER_H
#define PANTHERA_READER_H

#include <sysc/communication/sc_interface.h>
#include <sysc/communication/sc_port.h>

namespace ampere {
namespace sc {

template<typename T>
class Reader : public sc_core::sc_interface
{
public:
    virtual bool can_read() const = 0;
    virtual T read() = 0;
};

template<typename T>
using Read = sc_core::sc_port<Reader<T>>;

template<typename T>
using ReadOpt = sc_core::sc_port<Reader<T>, 1, sc_core::SC_ZERO_OR_MORE_BOUND>;

} // namespace sc
} // namespace ampere

#endif // PANTHERA_READER_H
