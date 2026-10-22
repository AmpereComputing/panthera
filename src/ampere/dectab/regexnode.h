//
// Copyright (c) 2019 Ampere Computing. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause
//
//
// Copyright (C) 2018, Ampere Computing
//
#ifndef AMPERE_DECTAB_REGEXNODE_H
#define AMPERE_DECTAB_REGEXNODE_H

#include <iostream>
#include <string>
#include <tuple>
#include <vector>

namespace ampere {
namespace dectab {

class RegexNode
{
public:
    enum class Op {
        EMPTY,
        SYM,
        CAT,
        EXCL,
        OR,
        LABEL,
        FAIL
    };

    RegexNode(Op op, int symbol, RegexNode *left, RegexNode *right) : op(op), symbol(symbol), left(left), right(right)
    {
    }

    bool matches_empty() const;
    std::pair<int, int> width() const;

    Op op;
    int symbol;
    RegexNode *left;
    RegexNode *right;

    int inner_state{-1};
    int outer_state{-1};
    bool is_accepting{false};

    std::vector<std::tuple<RegexNode *, int, int>> inner_edges;
    std::vector<std::pair<RegexNode *, int>> outer_edges;
};

std::ostream &operator<<(std::ostream &os, const RegexNode::Op op);

} // namespace dectab
} // namespace ampere

#endif // AMPERE_DECTAB_REGEXNODE_H

/*
 * vim: sw=4:ts=4:et:si:cindent:cino=\:0,(0,g0,t0:
 */
