//
// Copyright (c) 2019 Ampere Computing. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause
//
//
// Copyright (C) 2018, Ampere Computing
//

#include "parser.h"
#include "regexnode.h"

namespace ampere {
namespace dectab {

RegexNode* Parser::parse(std::istream& input)
{
    is = &input;
    lineno = 1;
    column = 0;
    in_pattern = false;
    advance();
    if (input.eof())
        return nullptr;
    return parse_alt(1);
}

RegexNode* Parser::parse_alt(int label)
{
    auto left = builder.make_regex(RegexNode::Op::LABEL, label, parse_or());
    builder.set_label_location(label, current_file, lineno);

    if (take('\n') && next != -1)
        return builder.make_regex(RegexNode::Op::OR, 0, left, parse_alt(label + 1));

    return left;
}

RegexNode* Parser::parse_or()
{
    auto left = parse_excl();
    if (take('|'))
        return builder.make_regex(RegexNode::Op::OR, 0, left, parse_or());
    return left;
}

RegexNode* Parser::parse_excl()
{
    auto left = parse_cat();
    if (take('!')) {
        auto right = parse_excl();
        auto lw = left->width();
        auto rw = right->width();
        if (lw.first != lw.second || rw.first != rw.second || lw.first != rw.first)
            throw Error(current_file, lineno, column, "operands of ! must be equal widths");
        return builder.make_regex(RegexNode::Op::EXCL, 0, left, right);
    }
    return left;
}

RegexNode* Parser::parse_cat()
{
    auto left = parse_bit();
    if (next == -1 || next == '|' || next == '!' || next == ')' || next == '\n')
        return left;
    return builder.make_regex(RegexNode::Op::CAT, 0, left, parse_cat());
}

RegexNode* Parser::parse_bit()
{
    if (take('0')) {
        return builder.make_regex(RegexNode::Op::SYM, '0');
    } else if (take('1')) {
        return builder.make_regex(RegexNode::Op::SYM, '1');
    } else if (take('x') || take('-')) {
        return builder.make_regex(RegexNode::Op::OR,
                                  0,
                                  builder.make_regex(RegexNode::Op::SYM, '0'),
                                  builder.make_regex(RegexNode::Op::SYM, '1'));
    } else if (take('(')) {
        auto re = parse_or();
        if (!take(')'))
            throw Error(current_file, lineno, column, "parse error; missing ')'");
        return re;
    } else {
        throw Error(current_file, lineno, column, "parse error");
    }
}

bool Parser::take(char c)
{
    if (next == c) {
        advance();
        return true;
    }

    return false;
}

void Parser::advance()
{
    skip_whitespace();

    if (*is) {
        int ch = is->get();
        if (ch != '\n') {
            in_pattern = true;
            ++column;
        }
        next = ch;
    } else {
        next = -1;
    }
}

void Parser::skip_whitespace()
{
    bool in_comment = false;

    while (*is && (isspace(is->peek()) || is->peek() == '#' || in_comment)) {
        int ch = is->get();

        if (ch == '\n') {
            if (!handle_line_directive(comment_text))
                ++lineno;

            in_comment = false;
            comment_text.clear();

            column = 0;

            if (in_pattern) {
                is->unget();
                in_pattern = false;
                return;
            }
        } else if (ch == '#') {
            in_comment = true;
            ++column;
        } else {
            if (in_comment)
                comment_text += ch;
            ++column;
        }
    }
}

bool Parser::handle_line_directive(std::string& comment)
{
    if (comment.length() > 4 && comment.substr(0, 4) == "line") {
        auto sep = comment.find(':');
        if (sep != std::string::npos) {
            current_file = comment.substr(5, sep - 5);
            lineno = std::stoi(comment.substr(sep + 1));
            return true;
        }
    }
    return false;
}

} // namespace dectab
} // namespace ampere

/*
 * vim: sw=4:ts=4:et:si:cindent:cino=\:0,(0,g0,t0:
 */
