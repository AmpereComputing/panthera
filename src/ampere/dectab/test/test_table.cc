//
// Copyright (c) 2019 Ampere Computing. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause
//
//
// Copyright (C) 2018, Ampere Computing
//
#include "table.h"

#include <gtest/gtest.h>
#include <boost/filesystem/operations.hpp>
#include <boost/filesystem/path.hpp>
#include <fstream>
#include <iterator>
#include <set>

#include "dfa.h"
#include "parser.h"

using namespace ampere::dectab;

class TableTest : public ::testing::Test
{
protected:
    Builder builder;
    Parser parser{builder};
    DFA dfa{builder};
    Table table;

    RegexNode* build(const std::string& s)
    {
        std::istringstream iss(s);
        return table.build(dfa.build(parser.parse(iss)));
    }

    RegexNode* get_transition(RegexNode* state, int byte, int label = -1)
    {
        EXPECT_GT(state->outer_edges.size(), byte);
        EXPECT_NE(state->outer_edges[byte].first, nullptr);
        if (label != -1) {
            EXPECT_EQ(state->outer_edges[byte].second, label);
        }
        return state->outer_edges[byte].first;
    }

    void no_transitions_except(RegexNode* state, std::initializer_list<int> exceptions)
    {
        std::set<int> exception_set(exceptions.begin(), exceptions.end());

        EXPECT_EQ(state->outer_edges.size(), 256);

        for (int b = 0; b < 256; b++) {
            if (exception_set.find(b) != exception_set.end())
                EXPECT_NE(state->outer_edges[b].first, nullptr);
            else
                EXPECT_EQ(state->outer_edges[b].first, nullptr);
        }
    }

    std::string find_path(const std::string& file)
    {
        boost::filesystem::path path{file};
        boost::filesystem::path up{".."};

        for (int i = 0; i < 10; i++) {
            if (boost::filesystem::exists(path))
                return path.string();

            path = up / path;
        }

        throw std::runtime_error{std::string{"can't find file "} + file};
    }
};

TEST_F(TableTest, CreateTableOfNonByteSizeFails)
{
    EXPECT_THROW(build("0"), std::runtime_error);
    EXPECT_THROW(build("010101010"), std::runtime_error);
}

TEST_F(TableTest, CreateTableOfByteSizeSucceeds)
{
    EXPECT_NO_THROW(build("00000000"));
}

TEST_F(TableTest, ZeroByteYieldsOneStateTable)
{
    RegexNode* start = build("00000000");
    EXPECT_EQ(start->outer_state, 0);
    ASSERT_EQ(start->outer_edges.size(), 256);
    ASSERT_NE(start->outer_edges[0].first, nullptr);
    EXPECT_TRUE(start->outer_edges[0].first->is_accepting);

    for (int b = 1; b < 256; b++)
        ASSERT_EQ(start->outer_edges[b].first, nullptr);
}

TEST_F(TableTest, ZeroAndOneBytesHaveLabels)
{
    RegexNode* start = build("00000000\n00000001");

    EXPECT_EQ(start->outer_state, 0);
    ASSERT_EQ(start->outer_edges.size(), 256);
    ASSERT_NE(start->outer_edges[0].first, nullptr);
    EXPECT_TRUE(start->outer_edges[0].first->is_accepting);
    ASSERT_NE(start->outer_edges[1].first, nullptr);
    EXPECT_TRUE(start->outer_edges[1].first->is_accepting);
    EXPECT_EQ(start->outer_edges[0].second, 1);
    EXPECT_EQ(start->outer_edges[1].second, 2);

    for (int b = 2; b < 256; b++)
        EXPECT_EQ(start->outer_edges[b].first, nullptr);
}

TEST_F(TableTest, TwoByteTable)
{
    RegexNode* start = build("00000000 00000000\n00000001 00000001\n00000010 00000010");
    no_transitions_except(start, {0, 1, 2});

    auto t0 = get_transition(start, 0, 1);
    EXPECT_TRUE(get_transition(t0, 0)->is_accepting);
    no_transitions_except(t0, {0});

    auto t1 = get_transition(start, 1, 2);
    EXPECT_TRUE(get_transition(t1, 1)->is_accepting);
    no_transitions_except(t1, {1});

    auto t2 = get_transition(start, 2, 3);
    EXPECT_TRUE(get_transition(t2, 2)->is_accepting);
    no_transitions_except(t2, {2});
}

TEST_F(TableTest, PrintSimpleTable)
{
    std::ostringstream oss;
    build("00000000\n00000001");
    table.print_transitions(oss);
    std::ifstream simple_table{find_path("src/ampere/dectab/test/simple_table.gold")};
    ASSERT_TRUE(simple_table);
    std::string table_contents(std::istreambuf_iterator<char>{simple_table}, {});
    EXPECT_EQ(oss.str(), table_contents);
}

TEST_F(TableTest, PrintSimpleDecoder)
{
    std::ostringstream oss;
    build("00000000\n00000001");
    table.print_decoder("decode", oss);
    std::ifstream simple_table{find_path("src/ampere/dectab/test/simple_decoder.gold")};
    ASSERT_TRUE(simple_table);
    std::string table_contents(std::istreambuf_iterator<char>{simple_table}, {});
    EXPECT_EQ(oss.str(), table_contents);
}

/*
 * vim: sw=4:ts=4:et:si:cindent:cino=\:0,(0,g0,t0:
 */
