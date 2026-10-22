//
// Copyright (c) 2018-2019 Ampere Computing. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause
//

#include <gtest/gtest.h>
#include "ampere/archx/addr.h"

namespace ampere::addr {

using namespace ampere::archx;

TEST(testAddr, testConstructor)
{
    PhysAddr paddr(0xdeadbeef);
    ASSERT_EQ(PhysAddr(0xdeadbeef), paddr);

    PhysAddr copy(paddr);
    ASSERT_EQ(PhysAddr(0xdeadbeef), copy);
}

TEST(testAddr, testAssignment)
{
    PhysAddr paddr(0xdeadbeef);

    PhysAddr another;
    ASSERT_EQ(PhysAddr(0), another);

    another = paddr;
    ASSERT_EQ(PhysAddr(0xdeadbeef), another);
}

TEST(testAddr, testAnd)
{
    PhysAddr paddr(0xdeadbeef);
    PhysAddr mask(0xffff0000);

    ASSERT_EQ(PhysAddr(0xdead0000), paddr & mask);
    ASSERT_EQ(PhysAddr(0xdead0000), paddr & 0xffff0000);
}

TEST(testAddr, testAddSub)
{
    PhysAddr paddr(0xdeadbeef0000);

    paddr += 0x4;
    ASSERT_EQ(PhysAddr(0xdeadbeef0004), paddr);

    ASSERT_EQ(PhysAddr(0xdeadbeef0008), paddr + 0x4);

    ASSERT_EQ(PhysAddr(0xdeadbeef0000), paddr - 0x4);

    paddr -= 0x4;
    ASSERT_EQ(PhysAddr(0xdeadbeef0000), paddr);
}

TEST(testAddr, testAddSubPages)
{
    PhysAddr paddr(0xdeadbeef0000);

    paddr += PageSize{PageSize::Size::PS_64KB};
    ASSERT_EQ(PhysAddr(0xdeadbef00000), paddr);

    paddr -= PageSize{PageSize::Size::PS_4KB};
    ASSERT_EQ(PhysAddr(0xdeadbeeff000), paddr);

    ASSERT_EQ(PhysAddr(0xdeadbef00000), paddr + PageSize{PageSize::Size::PS_4KB});

    ASSERT_EQ(PhysAddr(0xdeadbeeef000), paddr - PageSize{PageSize::Size::PS_64KB});
}

TEST(testAddr, testExtractRange)
{
    PhysAddr addr(0b0101010101010100001111000111001100111010);
    ASSERT_EQ(0b0, addr.extract_range(0, 0));
    ASSERT_EQ(0b1, addr.extract_range(1, 1));
    ASSERT_EQ(0b0, addr.extract_range(2, 2));
    ASSERT_EQ(0b1, addr.extract_range(3, 3));
    ASSERT_EQ(0b1010, addr.extract_range(3, 0));
    ASSERT_EQ(0b0011, addr.extract_range(7, 4));
    ASSERT_EQ(0b001100, addr.extract_range(10, 6));
    ASSERT_EQ(0b0101010101010100001111000111001100111010, addr.extract_range(39, 0));
    ASSERT_EQ(addr.extract_range(39, 0), addr.extract_lower_range(39));
    ASSERT_EQ(0b10101010101010000111100011100110011101, addr.extract_range(38, 1));
    ASSERT_ANY_THROW(addr.extract_range(0, 1));
    ASSERT_ANY_THROW(addr.extract_range(77, 0));

    PhysAddr ones(0xffffffffffffffff);
    ASSERT_EQ(0b11111, ones.extract_range(10, 6));
    ASSERT_EQ(0xffffffffffffffff, ones.extract_range(63, 0));
    ASSERT_EQ(ones.extract_range(63, 0), ones.extract_upper_range(0));
}

TEST(testAddr, testExtractBits)
{
    PhysAddr addr(0b0101010101010100001111000111001100111010);
    ASSERT_EQ(addr.extract_bits({63, 62, 61, 60, 59, 58, 57, 56, 55, 54, 53, 52, 51, 50, 49, 48, 47, 46, 45, 44, 43, 42,
                                 41, 40, 39, 38, 37, 36, 35, 34, 33, 32, 31, 30, 29, 28, 27, 26, 25, 24, 23, 22, 21, 20,
                                 19, 18, 17, 16, 15, 14, 13, 12, 11, 10, 9,  8,  7,  6,  5,  4,  3,  2,  1,  0}),
              addr.value());
    ASSERT_EQ(addr.extract_bits({}), 0UL);
    ASSERT_EQ(addr.extract_bits({0, 5, 6, 19}), 0b0101);
    ASSERT_EQ(addr.extract_bits({18, 29, 1, 36, 23, 33}), 0b101100);

    ASSERT_ANY_THROW(
        addr.extract_bits({0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
                           0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
                           0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0})); // can't extract 65 bits
    ASSERT_ANY_THROW(addr.extract_bits({64, 3, 63}));                                        // bit 64 doesn't exist
}

TEST(testAddr, testExtractBit)
{
    PhysAddr addr(0b0101010101010100001111000111001100111010);
    EXPECT_FALSE(addr[0]);
    EXPECT_TRUE(addr[1]);
    EXPECT_FALSE(addr[2]);
    EXPECT_TRUE(addr[3]);
    EXPECT_TRUE(addr.extract_bit(4));
    EXPECT_TRUE(addr.extract_bit(5));
    EXPECT_FALSE(addr.extract_bit(6));
    EXPECT_FALSE(addr.extract_bit(7));
}

TEST(testAddr, testSameCacheLine)
{
    PhysAddr addr(0x1000);
    PhysAddr same(0x1004);
    PhysAddr diff(0x1100);

    ASSERT_TRUE(addr.same_cache_line(same));
    ASSERT_FALSE(addr.same_cache_line(diff));
}

TEST(testAddr, testSameHalfCacheLine)
{
    PhysAddr addr(0x1000);
    PhysAddr same(0x1010);
    PhysAddr diff(0x1020);

    ASSERT_TRUE(addr.same_half_cache_line(same));
    ASSERT_FALSE(addr.same_half_cache_line(diff));
}

TEST(testAddr, testNextLine)
{
    PhysAddr addr(0x1000);
    PhysAddr next_addr(0x1004);
    PhysAddr next_line(0x1040);

    ASSERT_EQ(addr.next_line_address(), next_line);
    ASSERT_EQ(next_addr.next_line_address(), next_line);
}

TEST(testAddr, testFromLineNumber)
{
    PhysAddr paddr(0xdeadbeef);
    ASSERT_EQ(PhysAddr::from_line_number(paddr.line_number()), paddr.line_address());
}

TEST(testAddr, testSamePage)
{
    PhysAddr addr(0x1000);
    PhysAddr same_page(0x1004);
    PhysAddr different_page_4k(0x2040);
    PhysAddr different_page_64k(0x11040);

    ASSERT_TRUE(addr.same_page(same_page, PageSize::Size::PS_4KB));
    ASSERT_FALSE(addr.same_page(different_page_4k, PageSize::Size::PS_4KB));
    ASSERT_FALSE(addr.same_page_4k(different_page_4k));
    ASSERT_TRUE(addr.same_page(different_page_4k, PageSize::Size::PS_64KB));
    ASSERT_FALSE(addr.same_page(different_page_64k, PageSize::Size::PS_4KB));
    ASSERT_FALSE(addr.same_page_4k(different_page_64k));
    ASSERT_FALSE(addr.same_page(different_page_64k, PageSize::Size::PS_64KB));
}

TEST(testAddr, testPageAddress)
{
    VirtAddr addr(0xffff589fe718);
    VirtAddr addr_4k(0xffff589fe000);
    VirtAddr addr_64k(0xffff589f0000);

    ASSERT_EQ(addr.page_address(PageSize::Size::PS_4KB), addr_4k);
    ASSERT_NE(addr.page_address(PageSize::Size::PS_4KB), addr_64k);
    ASSERT_EQ(addr.page_address(PageSize::Size::PS_64KB), addr_64k);
    ASSERT_NE(addr.page_address(PageSize::Size::PS_64KB), addr_4k);
}

TEST(testAddr, testPageNumber)
{
    VirtAddr addr(0xffff589fe718);

    ASSERT_EQ(addr.page_number_4k(), 0xffff589fe);
    ASSERT_EQ(addr.page_number(PageSize::Size::PS_4KB), 0xffff589fe);
    ASSERT_EQ(addr.page_number(PageSize::Size::PS_2MB), 0x7fffac4);
    ASSERT_EQ(addr.page_number(PageSize::Size::PS_64KB), 0xffff589f);
    ASSERT_EQ(addr.page_number(PageSize::Size::PS_4TB), 0x3f);
}

TEST(testAddr, testPageOffset)
{
    VirtAddr addr(0xffff589fe718);

    ASSERT_EQ(addr.page_offset_4k(), 0x1c);
    ASSERT_EQ(addr.page_offset(PageSize::Size::PS_4KB), 0x1c);
    ASSERT_EQ(addr.page_offset(PageSize::Size::PS_64KB), 0x39c);
    ASSERT_EQ(addr.page_offset(PageSize::Size::PS_4TB), 0xffd627f9c);
}

TEST(testAddr, testChunkBitmap)
{
    VirtAddr addr1(0x123456780);
    VirtAddr addr2(0x123456787);
    VirtAddr addr3(0x1234567fc);

    ASSERT_EQ(addr1.chunk_bitmap(1), 0x1);
    ASSERT_EQ(addr1.chunk_bitmap(8), 0x1);
    ASSERT_EQ(addr1.chunk_bitmap(9), 0x3);
    ASSERT_EQ(addr1.chunk_bitmap(16), 0x3);

    ASSERT_EQ(addr2.chunk_bitmap(1), 0x1);
    ASSERT_EQ(addr2.chunk_bitmap(2), 0x3);
    ASSERT_EQ(addr2.chunk_bitmap(4), 0x3);
    ASSERT_EQ(addr2.chunk_bitmap(8), 0x3);
    ASSERT_EQ(addr2.chunk_bitmap(16), 0x7);

    ASSERT_EQ(addr3.chunk_bitmap(1), 0x80);
    ASSERT_EQ(addr3.chunk_bitmap(2), 0x80);
    ASSERT_EQ(addr3.chunk_bitmap(4), 0x80);
    ASSERT_EQ(addr3.chunk_bitmap(8), 0x81);
    ASSERT_EQ(addr3.chunk_bitmap(16), 0x83);
}

TEST(testAddr, testMaskedAddress)
{
    VirtAddr addr(0xffff589fe713);
    VirtAddr addr_04b(0xffff589fe710);
    VirtAddr addr_64b(0xffff589fe700);

    ASSERT_EQ(addr.masked_address(1), addr);
    ASSERT_EQ(addr.masked_address(4), addr_04b);
    ASSERT_EQ(addr.masked_address(64), addr_64b);
}

TEST(testAddr, testUnMaskedAddress)
{
    VirtAddr addr(0xffff589fe711);
    VirtAddr addr_04b(0xffff589fe713);
    VirtAddr addr_64b(0xffff589fe73f);

    ASSERT_EQ(addr.unmasked_address(1), addr);
    ASSERT_EQ(addr.unmasked_address(4), addr_04b);
    ASSERT_EQ(addr.unmasked_address(64), addr_64b);
}

TEST(testAddr, testAddressRangeCovers)
{
    ASSERT_TRUE(address_range_a_covers_b(VirtAddr(0x10), 1, VirtAddr(0x10), 1));
    ASSERT_TRUE(address_range_a_covers_b(VirtAddr(0x10), 2, VirtAddr(0x10), 2));
    ASSERT_TRUE(address_range_a_covers_b(VirtAddr(0x10), 8, VirtAddr(0x10), 8));

    ASSERT_TRUE(address_range_a_covers_b(VirtAddr(0x10), 8, VirtAddr(0x10), 4));
    ASSERT_TRUE(address_range_a_covers_b(VirtAddr(0x10), 8, VirtAddr(0x14), 4));
    ASSERT_TRUE(address_range_a_covers_b(VirtAddr(0x10), 8, VirtAddr(0x14), 1));

    ASSERT_FALSE(address_range_a_covers_b(VirtAddr(0x10), 8, VirtAddr(0x10), 9));
    ASSERT_FALSE(address_range_a_covers_b(VirtAddr(0x10), 8, VirtAddr(0x11), 8));
    ASSERT_FALSE(address_range_a_covers_b(VirtAddr(0x10), 8, VirtAddr(0x0), 8));
    ASSERT_FALSE(address_range_a_covers_b(VirtAddr(0x10), 8, VirtAddr(0x110), 8));
}

TEST(testAddr, testAddressRangeIntersects)
{
    ASSERT_TRUE(address_range_a_intersects_b(VirtAddr(0x10), 1, VirtAddr(0x10), 1, 1));
    ASSERT_TRUE(address_range_a_intersects_b(VirtAddr(0x10), 2, VirtAddr(0x10), 2, 1));

    ASSERT_TRUE(address_range_a_intersects_b(VirtAddr(0x10), 2, VirtAddr(0x11), 2, 1));
    ASSERT_TRUE(address_range_a_intersects_b(VirtAddr(0x11), 2, VirtAddr(0x10), 2, 1));

    ASSERT_TRUE(address_range_a_intersects_b(VirtAddr(0x10), 8, VirtAddr(0x10), 4, 1));
    ASSERT_TRUE(address_range_a_intersects_b(VirtAddr(0x10), 1, VirtAddr(0x10), 4, 1));
    ASSERT_TRUE(address_range_a_intersects_b(VirtAddr(0x11), 1, VirtAddr(0x10), 4, 1));
    ASSERT_TRUE(address_range_a_intersects_b(VirtAddr(0x12), 1, VirtAddr(0x10), 4, 1));
    ASSERT_TRUE(address_range_a_intersects_b(VirtAddr(0x13), 1, VirtAddr(0x10), 4, 1));
    ASSERT_TRUE(address_range_a_intersects_b(VirtAddr(0x13), 2, VirtAddr(0x10), 4, 1));

    ASSERT_TRUE(address_range_a_intersects_b(VirtAddr(0xf), 5, VirtAddr(0x10), 4, 1));
    ASSERT_TRUE(address_range_a_intersects_b(VirtAddr(0x10), 4, VirtAddr(0xf), 5, 1));

    ASSERT_FALSE(address_range_a_intersects_b(VirtAddr(0x14), 1, VirtAddr(0x10), 4, 1));
    ASSERT_FALSE(address_range_a_intersects_b(VirtAddr(0x0f), 1, VirtAddr(0x10), 4, 1));

    ASSERT_FALSE(address_range_a_intersects_b(VirtAddr(0x13), 1, VirtAddr(0x12), 1, 1));
    ASSERT_TRUE(address_range_a_intersects_b(VirtAddr(0x13), 1, VirtAddr(0x12), 1, 2));

    ASSERT_FALSE(address_range_a_intersects_b(VirtAddr(0x10), 1, VirtAddr(0x12), 1, 1));
    ASSERT_TRUE(address_range_a_intersects_b(VirtAddr(0x10), 1, VirtAddr(0x12), 1, 4));

    ASSERT_FALSE(address_range_a_intersects_b(VirtAddr(0x10), 5, VirtAddr(0x15), 5, 1));
    ASSERT_TRUE(address_range_a_intersects_b(VirtAddr(0x10), 5, VirtAddr(0x15), 5, 2));

    ASSERT_FALSE(address_range_a_intersects_b(VirtAddr(0x10), 1, VirtAddr(0x12), 1, 1));
    ASSERT_TRUE(address_range_a_intersects_b(VirtAddr(0x10), 1, VirtAddr(0x12), 1, 4));

    ASSERT_FALSE(address_range_a_intersects_b(VirtAddr(0x04), 4, VirtAddr(0x08), 4, 1));
    ASSERT_FALSE(address_range_a_intersects_b(VirtAddr(0x04), 4, VirtAddr(0x08), 4, 4));
    ASSERT_FALSE(address_range_a_intersects_b(VirtAddr(0x04), 4, VirtAddr(0x08), 4, 8));
}

TEST(AddrRange, InitFromAddrs)
{
    AddrRange<VirtAddr> range(0x1_va, 0x4_va);
    EXPECT_EQ(range.addr, 0x1_va);
    EXPECT_EQ(range.size, 3U);
}

TEST(AddrRange, InitFromRange)
{
    AddrRange<VirtAddr> range(0x1_va, 4);
    EXPECT_EQ(range.addr, 0x1_va);
    EXPECT_EQ(range.size, 4U);
}

TEST(AddrRange, Empty)
{
    AddrRange<VirtAddr> range;
    EXPECT_TRUE(range.empty());
}

TEST(AddrRange, LastByte)
{
    AddrRange<VirtAddr> range(0x1_va, 0x4_va);
    EXPECT_EQ(range.last_byte_addr(), 0x3_va);
}

TEST(AddrRange, Expand)
{
    AddrRange<VirtAddr> range(0x4_va, 0x9_va);
    EXPECT_EQ(range.addr, 0x4_va);
    EXPECT_EQ(range.size, 5U);

    range.expand({0x2_va, 0x6_va});
    EXPECT_EQ(range.addr, 0x2_va);
    EXPECT_EQ(range.size, 7U);

    range.expand({0x4_va, 0xf_va});
    EXPECT_EQ(range.addr, 0x2_va);
    EXPECT_EQ(range.size, 13U);
}

} // namespace ampere::addr

/*
 * vim: sw=4:ts=4:et:si:cindent:cino=\:0,(0,g0,t0:
 */
