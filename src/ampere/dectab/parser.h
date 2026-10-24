//
// Copyright (c) 2019 Ampere Computing. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause
//
//
// Copyright (C) 2018, Ampere Computing
//
#ifndef AMPERE_DECTAB_PARSER_H
#define AMPERE_DECTAB_PARSER_H

#include <istream>

#include "builder.h"
#include "regexnode.h"

namespace ampere {
namespace dectab {

class Parser
{
public:
    Parser(Builder& builder) : builder(builder) {}
    RegexNode* parse(std::istream& input);

    class Error : public std::runtime_error
    {
    public:
        Error(std::string& file, int lineno, int column, const std::string& msg)
            : std::runtime_error(msg), file(file), lineno(lineno), column(column)
        {
        }

        std::string file;
        int lineno;
        int column;
    };

private:
    Builder& builder;
    std::istream* is;
    std::string current_file{"input"};
    std::string comment_text;
    int next;
    int lineno;
    int column;
    bool in_pattern;

    RegexNode* parse_alt(int label);
    RegexNode* parse_or();
    RegexNode* parse_excl();
    RegexNode* parse_cat();
    RegexNode* parse_bit();

    void advance();
    void skip_whitespace();
    bool take(char c);
    bool handle_line_directive(std::string& comment);
};

} // namespace dectab
} // namespace ampere

#endif // AMPERE_DECTAB_PARSER_H

/*
 * vim: sw=4:ts=4:et:si:cindent:cino=\:0,(0,g0,t0:
 */
