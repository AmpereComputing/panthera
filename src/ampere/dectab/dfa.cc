//
// Copyright (c) 2019 Ampere Computing. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause
//
//
// Copyright (C) 2018, Ampere Computing
//
#include "dfa.h"
#include <set>

namespace ampere {
namespace dectab {

RegexNode* DFA::build(RegexNode* start)
{
    m_start = nullptr;
    assign_state(start);

    std::deque<RegexNode*> work;
    work.push_back(start);

    while (!work.empty()) {
        auto state = work.front();
        work.pop_front();

        try_transition(state, '0', work);
        try_transition(state, '1', work);
    }

    m_start = start;
    return start;
}

void DFA::try_transition(RegexNode* state, int symbol, std::deque<RegexNode*>& work)
{
    auto next_state = derive(state, symbol);

    if (next_state != builder.fail()) {
        int label = 0;

        check_for_ambiguities(next_state);

        if (next_state->op == RegexNode::Op::LABEL) {
            label = next_state->symbol;
            next_state = next_state->left;
        }

        if (assign_state(next_state))
            work.push_back(next_state);

        state->inner_edges.emplace_back(next_state, symbol, label);
    }
}

bool DFA::assign_state(RegexNode* state)
{
    if (state->inner_state != -1)
        return false;

    state->inner_state = total_states++;
    state->is_accepting = state->matches_empty();
    return true;
}

RegexNode* DFA::derive(RegexNode* state, int symbol)
{
    switch (state->op) {
    case RegexNode::Op::EMPTY:
    case RegexNode::Op::FAIL:
        return builder.fail();
    case RegexNode::Op::SYM:
        return state->symbol == symbol ? builder.empty() : builder.fail();
    case RegexNode::Op::LABEL:
        return builder.label(derive(state->left, symbol), state->symbol);
    case RegexNode::Op::CAT: {
        auto left = builder.cat(derive(state->left, symbol), state->right);
        if (left->matches_empty())
            return builder.or_(left, derive(state->right, symbol));
        else
            return left;
    }
    case RegexNode::Op::EXCL:
        return builder.excl(derive(state->left, symbol), derive(state->right, symbol));
    case RegexNode::Op::OR:
        return builder.or_(derive(state->left, symbol), derive(state->right, symbol));
    }

    assert(false);
    return nullptr;
}

void DFA::dump(std::ostream& os)
{
    std::deque<RegexNode*> work;
    std::set<RegexNode*> considered;

    work.push_back(get_start());
    considered.insert(get_start());

    while (!work.empty()) {
        RegexNode* s = work.front();
        work.pop_front();

        os << "State " << s->inner_state;
        if (s->is_accepting)
            os << " (accepting)";
        os << std::endl;

        for (auto& e : s->inner_edges) {
            RegexNode* n;
            int symbol;
            int label;
            std::tie(n, symbol, label) = e;
            os << "\ton " << char(symbol) << " -> State " << n->inner_state;
            if (label)
                os << " [" << label << "]";
            os << std::endl;

            if (considered.find(n) == considered.end()) {
                work.push_back(n);
                considered.insert(n);
            }
        }
    }
}

void DFA::check_for_ambiguities(ampere::dectab::RegexNode* state)
{
    if (state->op == RegexNode::Op::OR && state->left->op == RegexNode::Op::LABEL
        && state->right->op == RegexNode::Op::LABEL && state->left->matches_empty() && state->right->matches_empty())
    {
        throw std::runtime_error(std::string("patterns at ") + location(state->left) + " and " + location(state->right)
                                 + " are ambiguous");
    }
}

std::string DFA::location(RegexNode* state)
{
    auto loc = builder.get_label_location(state->symbol);

    if (loc.first.empty())
        return std::to_string(state->symbol);
    else
        return loc.first + " line " + std::to_string(loc.second);
}

} // namespace dectab
} // namespace ampere

/*
 * vim: sw=4:ts=4:et:si:cindent:cino=\:0,(0,g0,t0:
 */
