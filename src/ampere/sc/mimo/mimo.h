//
// Copyright (c) 2019 Ampere Computing. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause
//

#ifndef PANTHERA_MIMO_H
#define PANTHERA_MIMO_H

#include "ampere/sc/connection.h"
#include "ampere/sc/reader.h"
#include "ampere/sc/putter.h"

namespace ampere {
namespace sc {

template<typename T>
class Mimo : public Connection, public Reader<T>, public Putter<T>
{
public:
    explicit Mimo(const char* name, Clock& clock);
};

template<typename T>
Mimo<T>::Mimo(const char* name, Clock& clock) : Connection(name, clock)
{
}

} // namespace sc
} // namespace ampere

#endif // PANTHERA_MIMO_H
