//
// Copyright (c) 2019 Ampere Computing. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause
//
//
// Copyright (C) 2018, Ampere Computing
//
#ifndef AMPERE_DECTAB_DFA_H
#define AMPERE_DECTAB_DFA_H

#include <deque>
#include <iostream>
#include <string>

#include "builder.h"
#include "regexnode.h"

namespace ampere {
namespace dectab {

class DFA
{
public:
    DFA(Builder& builder) : builder(builder), total_states(0), m_start(nullptr) {}
    RegexNode* build(RegexNode* start);
    RegexNode* derive(RegexNode* state, int symbol);
    RegexNode* get_start() const { return m_start; }
    void dump(std::ostream& os);

private:
    Builder& builder;
    int total_states;
    RegexNode* m_start;

    bool assign_state(RegexNode* state);
    void try_transition(RegexNode* state, int symbol, std::deque<RegexNode*>& work);
    void check_for_ambiguities(RegexNode* state);
    std::string location(RegexNode* label);
};

} // namespace dectab
} // namespace ampere

#endif // AMPERE_DECTAB_DFA_H

/*
 * vim: sw=4:ts=4:et:si:cindent:cino=\:0,(0,g0,t0:
 */
