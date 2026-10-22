//
// Copyright (c) 2023 Ampere Computing. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause
//

#pragma once

#include <iterator>

namespace ampere::util {

template<typename Iterator>
Iterator make_iterator(const std::reverse_iterator<Iterator> &it)
{
    return it.base() - 1;
}

} // namespace ampere::util

/*
 * vim: sw=4:ts=4:et:si:cindent:cino=\:0,(0,g0,t0:
 */
