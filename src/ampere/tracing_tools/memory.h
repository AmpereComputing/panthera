//
// Copyright (c) 2022 Ampere Computing. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause
//

#include <memory>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "ampere/archx/addr.h"
#include "ampere/archx/page_size.h"

#ifndef AMPERE_TRACING_MEMORY_H
#define AMPERE_TRACING_MEMORY_H

using namespace ampere::archx;

namespace ampere {
namespace tracer {

struct PhysPage
{
    PhysPage(PhysAddr start, PageSize size) : start{start.page_address(size)}, size{size} {}
    inline bool operator<(const PhysPage &other) const { return start < other.start; }
    inline bool operator==(const PhysPage &other) const { return start == other.start && size == other.size; }
    inline bool overlaps(const PhysPage &other) const { return !(other.end() < start || other.start > end()); }
    inline PhysAddr end() const { return (start + size) - 1; }
    PhysAddr start;
    PageSize size;
};

class PageContentsImpl
{
public:
    PageContentsImpl(PhysPage &page) : page{page}
    {
        size_t bytes = 1 << page.size.page_bits();
        size_t mask_bytes = 1 << (page.size.page_bits() - 3); // divide bytes in page by 8 (bits in a byte)
        contents = new uint8_t[bytes]();
        valid_mask = new uint8_t[mask_bytes]();
    }

    // Disable copy constructor and assignment
    PageContentsImpl(PageContentsImpl const &) = delete;
    PageContentsImpl &operator=(PageContentsImpl const &) = delete;

    /*
     * Write bytes to the given page. Overwrite existing bytes only if
     * `overwrite` is set to true.
     *
     * The common case here is expected to be writing a large (>8 byte),
     * aligned (to at least 8 bytes) chunk of data which has not previously
     * been written. Because of this, small or un-aligned writes or writes to
     * regions which have been partially or fully written previously is very
     * simply implemented (at the expense of performance).
     */
    void write(PhysAddr start, size_t bytes, const uint8_t *data, bool overwrite = false)
    {
#ifndef NDEBUG
        auto end = start + bytes - 1;
#endif
        assert(start >= page.start); // not writing prior to beginning of page
        assert(end <= page.end());   // not writing past end of page

        // Slow path if non-8-byte-aligned chunk at the beginning, or number of
        // bytes not divisible by 8
        if ((start.value() & 0x7) || (bytes & 0x7)) {
            return slow_write(start, bytes, data, overwrite);
        }

        // At this point, we know we are writing a region aligned to 8 bytes
        // that is a multiple of 8 bytes long. Now, ensure all masks are
        // previously empty (or overwrite==true), and set them before calling
        // memcpy(). If we find a non-empty mask and we are not overwriting,
        // fall back to slow path.
        if (!overwrite && !all_invalid(start, bytes)) {
            return slow_write(start, bytes, data, overwrite);
        }

        // Finally, we know we can write everything in `data` with wild
        // abandon.
        uint64_t page_offset = start - page.start;
        size_t mask_offset = page_offset >> 3;
        size_t mask_bytes = bytes >> 3;
        memcpy(contents + page_offset, data, bytes);
        memset(valid_mask + mask_offset, 0xff, mask_bytes);
    }

    const uint64_t read(PhysAddr start)
    {
        assert(start >= page.start);
        assert(start + 8 - 1 <= page.end());

        size_t byte_offset = start - page.start;

        return *(uint64_t *)(contents + byte_offset);
    }

    ~PageContentsImpl()
    {
        delete[] contents;
        delete[] valid_mask;
        contents = nullptr;
        valid_mask = nullptr;
    }

private:
    // Slow write path. Check every byte valid bit individually, write bytes
    // one-by-one.
    void slow_write(PhysAddr start, size_t bytes, const uint8_t *data, bool overwrite = false)
    {
        while (bytes) {
            size_t byte_offset = start - page.start;
            size_t group = byte_offset >> 3;
            size_t bit = byte_offset & 0x7;
            uint8_t mask = 1 << bit;

            if (overwrite || !(valid_mask[group] & mask)) {
                contents[byte_offset] = data[0];
                valid_mask[group] |= mask;
            }

            bytes--;
            start += 1;
            data++;
        }
    }

    // Returns true if `bytes` bytes starting at `start` do not have their
    // corresponding bit set in `valid_mask`. Assumes bytes is a multiple of 8
    // and that start is at least 8-byte aligned.
    bool all_invalid(PhysAddr start, size_t bytes)
    {
        assert(!(start.value() & 0x7));
        assert(!(bytes & 0x7));

        // Handle byte aligned to 8 bytes but not 64
        while (bytes && (start.value() & 0x3f)) {
            size_t byte_offset = start - page.start;
            size_t group = byte_offset >> 3;

            if (valid_mask[group])
                return false;

            start += 8;
            bytes -= 8;
        }

        // Handle mask aligned to 64 bytes in larger chunks
        uint64_t *valid_mask_64 = (uint64_t *)valid_mask;
        while (bytes >= 64) {
            size_t byte_offset = start - page.start;
            size_t group = byte_offset >> 6;

            if (valid_mask_64[group])
                return false;

            start += 64;
            bytes -= 64;
        }

        // Finish masks left over at end after processing all 64-byte-aligned
        // chunks
        while (bytes) {
            size_t byte_offset = start - page.start;
            size_t group = byte_offset >> 3;

            if (valid_mask[group])
                return false;

            start += 8;
            bytes -= 8;
        }

        return true;
    }

    PhysPage page;
    uint8_t *contents;
    uint8_t *valid_mask;
};

using PageContents = std::shared_ptr<PageContentsImpl>;
PageContents make_page_contents(PhysPage &page)
{
    return std::make_shared<PageContentsImpl>(page);
}

} // namespace tracer
} // namespace ampere

#endif /*AMPERE_TRACING_MEMORY_H*/

/*
 * vim: sw=4:ts=4:et:si:cindent:cino=\:0,(0,g0,t0:
 */
