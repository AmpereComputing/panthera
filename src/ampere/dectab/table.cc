//
// Copyright (c) 2019 Ampere Computing. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause
//
//
// Copyright (C) 2018, Ampere Computing
//
#include "table.h"
#include <deque>

namespace ampere {
namespace dectab {

RegexNode* Table::build(RegexNode* start)
{
    total_outer_states = 0;
    assign_outer_state(start);

    std::deque<RegexNode*> work;
    work.push_back(start);

    while (!work.empty()) {
        auto state = work.front();
        work.pop_front();

        for (int b = 0; b < 256; b++) {
            auto next_state_and_label = evaluate(state, b);
            RegexNode* next_state = next_state_and_label.first;
            int label = next_state_and_label.second;

            if (next_state) {
                if (assign_outer_state(next_state))
                    work.push_back(next_state);
                add_edge(state, next_state, b, label);
            }
        }
    }

    return start;
}

std::pair<RegexNode*, int> Table::evaluate(RegexNode* state, int byte)
{
    int label = 0;

    for (int bpos = 7; bpos >= 0; bpos--) {
        int symbol = (byte & (1 << bpos)) ? '1' : '0';

        RegexNode* next = nullptr;

        for (auto& e : state->inner_edges) {
            RegexNode* e_next;
            int e_symbol;
            int e_label;

            std::tie(e_next, e_symbol, e_label) = e;

            if (symbol == e_symbol) {
                next = e_next;
                label |= e_label;
                break;
            }
        }

        if (next) {
            if (next->is_accepting && bpos != 0)
                throw std::runtime_error{"patterns must be multiple whole bytes to create a table"};
            state = next;
        } else {
            return {nullptr, 0};
        }
    }

    return {state, label};
}

bool Table::assign_outer_state(RegexNode* state)
{
    if (state->outer_state != -1 || state->is_accepting)
        return false;

    states.push_back(state);

    state->outer_state = total_outer_states++;
    state->outer_edges.resize(256);

    return true;
}

void Table::add_edge(RegexNode* state, RegexNode* next, int byte, int label)
{
    state->outer_edges[byte] = std::make_pair(next, label);
}

void Table::print_decoder(const std::string& function, std::ostream& os)
{
    os << "int " << function << "(const unsigned char* bytes)\n{\n"
       << "  const int ERROR = 0;\n"
       << "  const int ACCEPT = 0xffff;\n\n"
       << "  static\n";

    print_transitions(os, "  ");

    os << "\n"
       << "  int state = 0;\n"
       << "  int label = 0;\n\n"
       << "  while (1) {\n"
       << "    int next = transitions[state][*bytes++];\n"
       << "    if (next == ERROR)\n"
       << "      return ERROR;\n"
       << "    state = next & 0xffff;\n"
       << "    label |= (next >> 16) & 0xffff;\n"
       << "    if (state == ACCEPT)\n"
       << "      break;\n"
       << "  }\n\n"
       << "  return label;\n"
       << "}" << std::endl;
}

void Table::print_transitions(std::ostream& os, const std::string& indent)
{
    os << indent << "int transitions[" << total_outer_states << "][256] = {\n";

    for (auto& s : states) {
        os << indent << "  { /* " << s->outer_state << " */\n    " << indent;
        for (int b = 0; b < 256; b++) {
            print_transition(os, s, b);

            if (b == 255)
                os << ",\n  " << indent;
            else if ((b % 16) == 15)
                os << ",\n    " << indent;
            else
                os << ", ";
        }
        os << "},\n";
    }

    os << indent << "};\n";
}

void Table::print_transition(std::ostream& os, RegexNode* state, int byte)
{
    auto edge = state->outer_edges[byte];

    if (!edge.first) {
        os << "ERROR";
    } else {
        int next_state = edge.first->outer_state;
        bool is_accepting = edge.first->is_accepting;
        int label = edge.second;

        if (is_accepting)
            os << "ACCEPT";
        else
            os << next_state;

        if (label)
            os << "|(" << label << "<<16)";
    }
}

} // namespace dectab
} // namespace ampere

/*
 * vim: sw=4:ts=4:et:si:cindent:cino=\:0,(0,g0,t0:
 */
