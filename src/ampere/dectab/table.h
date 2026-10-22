//
// Copyright (c) 2019 Ampere Computing. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause
//
//
// Copyright (C) 2018, Ampere Computing
//
#ifndef AMPERE_DECTAB_TABLE_H
#define AMPERE_DECTAB_TABLE_H

#include <iostream>
#include <utility>
#include <vector>

#include "regexnode.h"

namespace ampere {
namespace dectab {

class Table
{
public:
    RegexNode* build(RegexNode* start);
    void print_decoder(const std::string& function = "decode", std::ostream& os = std::cout);
    void print_transitions(std::ostream& os = std::cout, const std::string& indent = "");

private:
    int total_outer_states;
    std::vector<RegexNode*> states;

    bool assign_outer_state(RegexNode* state);
    std::pair<RegexNode*, int> evaluate(RegexNode* state, int byte);
    void add_edge(RegexNode* state, RegexNode* next, int byte, int label);
    void print_transition(std::ostream& os, RegexNode* state, int byte);
};

} // namespace dectab
} // namespace ampere

#endif // AMPERE_DECTAB_TABLE_H

/*
 * vim: sw=4:ts=4:et:si:cindent:cino=\:0,(0,g0,t0:
 */
