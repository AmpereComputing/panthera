//
// Copyright (c) 2022 Ampere Computing. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause
//

#include <stdarg.h>
#include <gtest/gtest.h>

#include "ampere/tracing_tools/memory.h"

using namespace ampere;
using namespace tracer;
using namespace archx;

class MemoryTest : public ::testing::Test
{
protected:
    void SetUp() override;
    void TearDown() override;
    PhysPage *page;
    PageContents contents;
};

void MemoryTest::SetUp()
{
    page = new PhysPage(0xf00ba7000_pa, PageSize::Size::PS_4KB);
    contents = make_page_contents(*page);
}

void MemoryTest::TearDown()
{
    delete page;
    page = nullptr;
    contents.reset();
}

TEST_F(MemoryTest, testInitialization)
{
    // initialized to 0
    ASSERT_EQ(contents->read(0xf00ba7080_pa), 0UL);
}

TEST_F(MemoryTest, testWrites)
{
    uint8_t buffer[256];
    uint64_t *buffer_64 = (uint64_t *)buffer;
    buffer_64[0] = 0xf000baa7deadbeef;

    // initially empty
    ASSERT_EQ(contents->read(0xf00ba748a_pa), 0UL);

    // first write (blank slate)
    contents->write(0xf00ba748a_pa, 1, buffer);
    ASSERT_EQ(contents->read(0xf00ba748a_pa), 0xefUL);

    // misaligned write, overlapping previous
    contents->write(0xf00ba7489_pa, 3, buffer, false /*no overwrite*/);
    ASSERT_EQ(contents->read(0xf00ba7488_pa), 0xadefef00UL);

    // aligned write, non-overlapping
    contents->write(0xf00ba7490_pa, 8, buffer, false /*no overwrite*/);
    ASSERT_EQ(contents->read(0xf00ba748a_pa), 0xbeef00000000adefUL);

    // misaligned write, overlapping on both ends
    contents->write(0xf00ba748a_pa, 8, buffer, false /*no overwrite*/);
    ASSERT_EQ(contents->read(0xf00ba748a_pa), 0xbeefbaa7deadadefUL);
}

TEST_F(MemoryTest, testOverwriting)
{
    uint8_t buffer[256];
    uint64_t *buffer_64 = (uint64_t *)buffer;
    buffer_64[0] = 0xf000baa7deadbeef;

    // initially empty
    ASSERT_EQ(contents->read(0xf00ba748a_pa), 0UL);

    // misaligned write, overlapping previous
    contents->write(0xf00ba7489_pa, 3, buffer, true /*overwrite*/);
    ASSERT_EQ(contents->read(0xf00ba7488_pa), 0xadbeef00UL);

    // aligned write, non-overlapping
    contents->write(0xf00ba7490_pa, 8, buffer, false /*no overwrite*/);
    ASSERT_EQ(contents->read(0xf00ba748a_pa), 0xbeef00000000adbeUL);

    // misaligned write, overlapping on both ends
    contents->write(0xf00ba748a_pa, 8, buffer, true /*overwrite*/);
    ASSERT_EQ(contents->read(0xf00ba748a_pa), 0xf000baa7deadbeefUL);
}

TEST_F(MemoryTest, testLargeWrite)
{
    uint8_t buffer[256];
    for (size_t i = 0; i < sizeof(buffer); i++) {
        buffer[i] = 0xa5;
    }
    buffer[0] = 0x12;
    buffer[255] = 0xef;

    // initially empty
    ASSERT_EQ(contents->read(0xf00ba748a_pa), 0UL);

    // Do large aligned write, check boundaries and a few places in the middle
    contents->write(0xf00ba7c00_pa, 256, buffer);
    ASSERT_EQ(contents->read(0xf00ba7bf8_pa), 0UL);
    ASSERT_EQ(contents->read(0xf00ba7c00_pa), 0xa5a5a5a5a5a5a512UL);
    ASSERT_EQ(contents->read(0xf00ba7c32_pa), 0xa5a5a5a5a5a5a5a5UL);
    ASSERT_EQ(contents->read(0xf00ba7ca9_pa), 0xa5a5a5a5a5a5a5a5UL);
    ASSERT_EQ(contents->read(0xf00ba7cf8_pa), 0xefa5a5a5a5a5a5a5UL);
    ASSERT_EQ(contents->read(0xf00ba7d00_pa), 0UL);
}

TEST_F(MemoryTest, testLargeOverlappingWrite)
{
    uint8_t buffer[256];
    for (size_t i = 0; i < sizeof(buffer); i++) {
        buffer[i] = 0xa5;
    }
    buffer[0] = 0x12;
    buffer[255] = 0xef;

    uint8_t smallwrite = 0x79;

    contents->write(0xf00ba7c85_pa, 1, &smallwrite);

    // Do large aligned write, check boundaries and a few places in the middle
    // (particularly where should not be overwriting 0x79)
    contents->write(0xf00ba7c00_pa, 256, buffer);
    ASSERT_EQ(contents->read(0xf00ba7bf8_pa), 0UL);
    ASSERT_EQ(contents->read(0xf00ba7c00_pa), 0xa5a5a5a5a5a5a512UL);
    ASSERT_EQ(contents->read(0xf00ba7c32_pa), 0xa5a5a5a5a5a5a5a5UL);
    ASSERT_EQ(contents->read(0xf00ba7c80_pa), 0xa5a579a5a5a5a5a5UL); // 0x79 not overwritten
    ASSERT_EQ(contents->read(0xf00ba7ca9_pa), 0xa5a5a5a5a5a5a5a5UL);
    ASSERT_EQ(contents->read(0xf00ba7cf8_pa), 0xefa5a5a5a5a5a5a5UL);
    ASSERT_EQ(contents->read(0xf00ba7d00_pa), 0UL);
}

/*
 * vim: sw=4:ts=4:et:si:cindent:cino=\:0,(0,g0,t0:
 */
