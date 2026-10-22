//
// Copyright (c) 2019 Ampere Computing. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause
//
#ifndef AMPERE_DECTAB_BUILDER_H
#define AMPERE_DECTAB_BUILDER_H

#include <boost/functional/hash.hpp>
#include <string>
#include <unordered_map>

#include "regexnode.h"

namespace ampere {
namespace dectab {

class Builder
{
public:
    RegexNode* make_regex(RegexNode::Op op, int symbol, RegexNode* left = nullptr, RegexNode* right = nullptr);

    RegexNode* empty() { return m_empty ? m_empty : (m_empty = make_regex(RegexNode::Op::EMPTY, 0)); }
    RegexNode* fail() { return m_fail ? m_fail : (m_fail = make_regex(RegexNode::Op::FAIL, 0)); }
    RegexNode* symbol(int sym) { return make_regex(RegexNode::Op::SYM, sym); }
    RegexNode* label(RegexNode* left, int sym);
    RegexNode* zero() { return symbol('0'); }
    RegexNode* one() { return symbol('1'); }
    RegexNode* cat(RegexNode* left, RegexNode* right);
    RegexNode* excl(RegexNode* left, RegexNode* right);
    RegexNode* or_(RegexNode* left, RegexNode* right);

    void set_label_location(int label, const std::string& file, int line);
    std::pair<std::string, int> get_label_location(int label);

private:
    using RegexKey = std::tuple<RegexNode::Op, int, RegexNode*, RegexNode*>;
    std::unordered_map<RegexKey, std::unique_ptr<RegexNode>, boost::hash<RegexKey>> regex_cache;
    std::unordered_map<int, std::pair<std::string, int>> label_location;

    RegexNode* m_empty{nullptr};
    RegexNode* m_fail{nullptr};
};

} // namespace dectab
} // namespace ampere

#endif // AMPERE_DECTAB_BUILDER_H

/*
 * vim: sw=4:ts=4:et:si:cindent:cino=\:0,(0,g0,t0:
 */
