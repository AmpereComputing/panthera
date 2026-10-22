//
// Copyright (c) 2019 Ampere Computing. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause
//
//
// Copyright (C) 2018, Ampere Computing
//
#include "regexnode.h"

namespace ampere {
namespace dectab {

bool RegexNode::matches_empty() const
{
    if (op == RegexNode::Op::EMPTY)
        return true;
    else if (op == RegexNode::Op::OR)
        return left->matches_empty() || right->matches_empty();
    else if (op == RegexNode::Op::LABEL)
        return left->matches_empty();
    else
        return false;
}

std::pair<int, int> RegexNode::width() const
{
    switch (op) {
    case RegexNode::Op::SYM:
        return {1, 1};
    case RegexNode::Op::CAT: {
        auto lw = left->width();
        auto rw = right->width();
        return {lw.first + rw.first, lw.second + rw.second};
    }
    case RegexNode::Op::EXCL:
        return left->width();
    case RegexNode::Op::OR: {
        auto lw = left->width();
        auto rw = right->width();
        return {std::min(lw.first, rw.first), std::max(lw.second, rw.second)};
    }
    case RegexNode::Op::LABEL:
        return left->width();
    default:
        return {0, 0};
    }
}

std::ostream& operator<<(std::ostream& os, const RegexNode::Op op)
{
    static const char* op_names[] = {"EMPTY", "SYM", "CAT", "OR", "LABEL", "FAIL"};

    return os << op_names[static_cast<int>(op)];
}

} // namespace dectab
} // namespace ampere

/*
 * vim: sw=4:ts=4:et:si:cindent:cino=\:0,(0,g0,t0:
 */
