//
// Copyright (c) 2019 Ampere Computing. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause
//
#include "builder.h"

namespace ampere {
namespace dectab {

RegexNode* Builder::make_regex(RegexNode::Op op, int symbol, RegexNode* left, RegexNode* right)
{
    auto key = std::make_tuple(op, symbol, left, right);
    auto entry = regex_cache.find(key);

    if (entry == regex_cache.end()) {
        auto re = new RegexNode(op, symbol, left, right);
        regex_cache.emplace(key, re);
        return re;
    } else {
        return entry->second.get();
    }
}

RegexNode* Builder::label(RegexNode* left, int symbol)
{
    if (left == fail())
        return fail();
    return make_regex(RegexNode::Op::LABEL, symbol, left);
}

RegexNode* Builder::cat(RegexNode* left, RegexNode* right)
{
    if (left == fail() || right == fail())
        return fail();
    else if (left == empty())
        return right;
    else if (right == empty())
        return left;
    else
        return make_regex(RegexNode::Op::CAT, 0, left, right);
}

RegexNode* Builder::excl(RegexNode* left, RegexNode* right)
{
    if (right == fail())
        return left;
    else if (right == empty())
        return fail();
    else
        return make_regex(RegexNode::Op::EXCL, 0, left, right);
}

RegexNode* Builder::or_(RegexNode* left, RegexNode* right)
{
    if (left == fail()) {
        return right;
    } else if (right == fail()) {
        return left;
    } else if (left == empty() && right == empty()) {
        return empty();
    } else {
        RegexKey key = std::make_tuple(RegexNode::Op::OR, 0, left, right);
        auto left_or_right = regex_cache.find(key);

        key = std::make_tuple(RegexNode::Op::OR, 0, right, left);
        auto right_or_left = regex_cache.find(key);

        if (left_or_right == regex_cache.end() && right_or_left != regex_cache.end())
            return make_regex(RegexNode::Op::OR, 0, right, left);
        else
            return make_regex(RegexNode::Op::OR, 0, left, right);
    }
}

void Builder::set_label_location(int label, const std::string& file, int line)
{
    label_location[label] = std::make_pair(file, line);
}

std::pair<std::string, int> Builder::get_label_location(int label)
{
    auto loc = label_location.find(label);

    if (loc == label_location.end())
        return {"", 0};
    else
        return {loc->second.first, loc->second.second};
}

} // namespace dectab
} // namespace ampere

/*
 * vim: sw=4:ts=4:et:si:cindent:cino=\:0,(0,g0,t0:
 */
