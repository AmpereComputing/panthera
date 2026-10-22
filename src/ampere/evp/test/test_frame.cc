//
// Copyright (c) 2021-2025 Ampere Computing. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause
//

#include "ampere/evp/frame.h"

#include <fmt/format.h>
#include <gtest/gtest.h>

using namespace ampere::evp;

TEST(TestFrame, CanCreateEmptyFrameOfInts)
{
    Frame<int> frame;
}

TEST(TestFrame, CanAddSeries)
{
    Frame<int> frame;
    frame.add_row(RowSeries<int>{{"a", 1}, {"b", 2}});

    ASSERT_EQ(frame.rows(), 1);
    ASSERT_EQ(frame.cols(), 2);

    EXPECT_EQ(frame.at(0, 0), 1);
    EXPECT_EQ(frame.at(0, 1), 2);

    frame.add_row(RowSeries<int>{{"a", 6}, {"z", 8}});

    ASSERT_EQ(frame.rows(), 2);
    ASSERT_EQ(frame.cols(), 3);

    EXPECT_EQ(frame.at(0, 0), 1);
    EXPECT_EQ(frame.at(0, 1), 2);
    EXPECT_EQ(frame.at(0, 2), 0); // default for z in row 0
    EXPECT_EQ(frame.at(1, 0), 6);
    EXPECT_EQ(frame.at(1, 1), 0); // default for b in row 1
    EXPECT_EQ(frame.at(1, 2), 8);
}

TEST(TestFrame, MakeColSeries)
{
    auto col = ColSeries("a", {20, 21, 22});
    EXPECT_EQ(col.second[0], 20);
    EXPECT_EQ(col.second[1], 21);
    EXPECT_EQ(col.second[2], 22);
}

TEST(TestFrame, CanCreateFrameFromColumns)
{
    Frame<int> frame{{"one", {1, 2, 3}}, {"two", {4, 5, 6}}, {"three", {7, 8, 9}}};

    ASSERT_EQ(frame.rows(), 3);
    ASSERT_EQ(frame.cols(), 3);

    EXPECT_EQ(frame.at(0, 0), 1);
    EXPECT_EQ(frame.at(0, 1), 7);
    EXPECT_EQ(frame.at(0, 2), 4);
    EXPECT_EQ(frame.at(1, 0), 2);
    EXPECT_EQ(frame.at(1, 1), 8);
    EXPECT_EQ(frame.at(1, 2), 5);
    EXPECT_EQ(frame.at(2, 0), 3);
    EXPECT_EQ(frame.at(2, 1), 9);
    EXPECT_EQ(frame.at(2, 2), 6);
}

TEST(TestFrame, CanCreateFrameFromRows)
{
    Frame<int> frame{{{"one", 1}, {"two", 4}, {"three", 7}},
                     {{"one", 2}, {"two", 5}, {"three", 8}},
                     {{"one", 3}, {"two", 6}, {"three", 9}}};

    ASSERT_EQ(frame.rows(), 3);
    ASSERT_EQ(frame.cols(), 3);

    EXPECT_EQ(frame.at(0, 0), 1);
    EXPECT_EQ(frame.at(0, 1), 7);
    EXPECT_EQ(frame.at(0, 2), 4);
    EXPECT_EQ(frame.at(1, 0), 2);
    EXPECT_EQ(frame.at(1, 1), 8);
    EXPECT_EQ(frame.at(1, 2), 5);
    EXPECT_EQ(frame.at(2, 0), 3);
    EXPECT_EQ(frame.at(2, 1), 9);
    EXPECT_EQ(frame.at(2, 2), 6);
}

TEST(TestFrame, CanIterateOverRow)
{
    Frame<int> frame{{"one", {1, 2, 3}}, {"two", {4, 5, 6}}, {"three", {7, 8, 9}}};

    // Note that columns are kept sorted (in this case, alphabetically).
    int row_expected[3][3] = {{1, 7, 4}, {2, 8, 5}, {3, 9, 6}};

    for (size_t row = 0; row < 3; ++row) {
        size_t col = 0;

        for (auto &v : frame.row(row))
            EXPECT_EQ(v, row_expected[row][col++]);
    }
}

TEST(TestFrame, CanIterateOverConstRow)
{
    const Frame<int> frame{{"one", {1, 2, 3}}, {"two", {4, 5, 6}}, {"three", {7, 8, 9}}};

    // Note that columns are kept sorted (in this case, alphabetically).
    int row_expected[3][3] = {{1, 7, 4}, {2, 8, 5}, {3, 9, 6}};

    for (size_t row = 0; row < 3; ++row) {
        size_t col = 0;

        for (auto &v : frame.row(row))
            EXPECT_EQ(v, row_expected[row][col++]);
    }
}
