//
// Copyright (c) 2019 Ampere Computing. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause
//
//
// Copyright (C) 2018, Ampere Computing
//
#include <gtest/gtest.h>

#include "parser.h"
#include "regexnode.h"

using namespace ampere::dectab;

class ParserTest : public ::testing::Test
{
protected:
    Builder builder;
    Parser parser{builder};

    RegexNode* parse(const std::string& s, bool keep_label = false)
    {
        std::istringstream iss(s);
        auto re = parser.parse(iss);
        if (re)
            return re->op == RegexNode::Op::LABEL && !keep_label ? re->left : re;
        else
            return nullptr;
    }
};

TEST_F(ParserTest, ZeroReturnsSymbolZeroNode)
{
    auto re = parse("0");
    EXPECT_EQ(re->op, RegexNode::Op::SYM);
    EXPECT_EQ(re->symbol, '0');
}

TEST_F(ParserTest, OneReturnsSymbolOneNode)
{
    auto re = parse("1");
    EXPECT_EQ(re->op, RegexNode::Op::SYM);
    EXPECT_EQ(re->symbol, '1');
}

TEST_F(ParserTest, XReturnsZeroOrOne)
{
    auto re = parse("x");
    EXPECT_EQ(re->op, RegexNode::Op::OR);
    EXPECT_EQ(re->left->op, RegexNode::Op::SYM);
    EXPECT_EQ(re->left->symbol, '0');
    EXPECT_EQ(re->right->op, RegexNode::Op::SYM);
    EXPECT_EQ(re->right->symbol, '1');
}

TEST_F(ParserTest, HyphenIsTheSameAsX)
{
    auto re1 = parse("x");
    auto re2 = parse("-");
    EXPECT_EQ(re1, re2);
}

TEST_F(ParserTest, AdjacentSymbolsReturnCat)
{
    auto re = parse("01");
    EXPECT_EQ(re->op, RegexNode::Op::CAT);
    EXPECT_EQ(re->left->op, RegexNode::Op::SYM);
    EXPECT_EQ(re->left->symbol, '0');
    EXPECT_EQ(re->right->op, RegexNode::Op::SYM);
    EXPECT_EQ(re->right->symbol, '1');
}

TEST_F(ParserTest, CatHasPrecedenceOverOr)
{
    auto re = parse("00|01");
    EXPECT_EQ(re->op, RegexNode::Op::OR);
    EXPECT_EQ(re->left->op, RegexNode::Op::CAT);
    EXPECT_EQ(re->right->op, RegexNode::Op::CAT);
}

TEST_F(ParserTest, SpacesAreIgnored)
{
    auto re = parse(" \t 0  ");
    EXPECT_EQ(re->op, RegexNode::Op::SYM);
    EXPECT_EQ(re->symbol, '0');
}

TEST_F(ParserTest, ParseErrorOnFirstCharacterReportsLine1Column1)
{
    try {
        parse("Z");
    } catch (Parser::Error& err) {
        EXPECT_EQ(err.lineno, 1);
        EXPECT_EQ(err.column, 1);
    }
}

TEST_F(ParserTest, ParseErrorOnBeginingOfSecondLineReportsLine2Column1)
{
    try {
        parse("0\nZ");
    } catch (Parser::Error& err) {
        EXPECT_EQ(err.lineno, 2);
        EXPECT_EQ(err.column, 1);
    }
}

TEST_F(ParserTest, ParseErrorReportsLineAndColumnNumberInMiddleOfLine)
{
    try {
        parse("1\n Z");
    } catch (Parser::Error& err) {
        EXPECT_EQ(err.lineno, 2);
        EXPECT_EQ(err.column, 2);
    }
}

TEST_F(ParserTest, NewlineCreatesLabeledOr)
{
    auto re = parse("0\n1", true);
    EXPECT_EQ(re->op, RegexNode::Op::OR);
    EXPECT_EQ(re->left->op, RegexNode::Op::LABEL);
    EXPECT_EQ(re->left->symbol, 1);
    EXPECT_EQ(re->right->op, RegexNode::Op::LABEL);
    EXPECT_EQ(re->right->symbol, 2);
}

TEST_F(ParserTest, CommentsAreIgnored)
{
    auto re = parse("1 # this is a comment");
    EXPECT_EQ(re->op, RegexNode::Op::SYM);
    EXPECT_EQ(re->symbol, '1');
}

TEST_F(ParserTest, CommentDoesNotDisturbNewlineBehavior)
{
    auto re = parse("0 # accept a 0\n1", true);
    EXPECT_EQ(re->op, RegexNode::Op::OR);
    EXPECT_EQ(re->left->op, RegexNode::Op::LABEL);
    EXPECT_EQ(re->left->symbol, 1);
    EXPECT_EQ(re->right->op, RegexNode::Op::LABEL);
    EXPECT_EQ(re->right->symbol, 2);
}

TEST_F(ParserTest, ParensCreateSubExpression)
{
    auto re = parse("0(0|1)");
    EXPECT_EQ(re->op, RegexNode::Op::CAT);
    EXPECT_EQ(re->right->op, RegexNode::Op::OR);
    EXPECT_EQ(re->right->left->op, RegexNode::Op::SYM);
    EXPECT_EQ(re->right->left->symbol, '0');
    EXPECT_EQ(re->right->right->op, RegexNode::Op::SYM);
    EXPECT_EQ(re->right->right->symbol, '1');
}

TEST_F(ParserTest, NotEqualBuildsExclusionNode)
{
    auto re = parse("x!1");
    EXPECT_EQ(re->op, RegexNode::Op::EXCL);
}

TEST_F(ParserTest, WidthOfSymbolIsOne)
{
    std::pair<int, int> w{1, 1};
    EXPECT_EQ(parse("1")->width(), w);
}

TEST_F(ParserTest, WidthOfCatIsNumberOfSymbols)
{
    std::pair<int, int> w{6, 6};
    EXPECT_EQ(parse("1xx001")->width(), w);
}

TEST_F(ParserTest, WidthOfOrIsMinMaxOfLeftAndRight)
{
    std::pair<int, int> w{1, 2};
    EXPECT_EQ(parse("1|01")->width(), w);
}

TEST_F(ParserTest, CannotExcludePatternOfUnequalWidth)
{
    EXPECT_THROW(parse("xx!1"), Parser::Error);
    EXPECT_THROW(parse("1xx!00"), Parser::Error);
}

TEST_F(ParserTest, TrailingNewlinesAreIgnored)
{
    EXPECT_NO_THROW(parse("10\n\n"));
}

TEST_F(ParserTest, MissingParenThrowsException)
{
    EXPECT_THROW(parse("(10"), Parser::Error);
    EXPECT_THROW(parse("10x((10)11"), Parser::Error);
}

TEST_F(ParserTest, WithNoLineDirectiveInputIsUsed)
{
    try {
        parse("a");
    } catch (Parser::Error& err) {
        EXPECT_EQ(err.file, "input");
        EXPECT_EQ(err.lineno, 1);
    }
}

TEST_F(ParserTest, LineDirectiveContinuesLineCount)
{
    try {
        parse("#line bar:20\n10\na");
    } catch (Parser::Error& err) {
        EXPECT_EQ(err.file, "bar");
        EXPECT_EQ(err.lineno, 21);
    }
}

TEST_F(ParserTest, EmptyStringIsOk)
{
    auto re = parse("");
    EXPECT_EQ(re, nullptr);
}

/*
 * vim: sw=4:ts=4:et:si:cindent:cino=\:0,(0,g0,t0:
 */
