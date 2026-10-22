//
// Copyright (c) 2022 Ampere Computing. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause
//

#pragma once

#include "ampere/sc/sc.h"
#include "ampere/util/enum.h"
#include "ampere/sc/transaction.h"

make_ns_enum(ampere::mesh, Type, Read, Write);
make_ns_enum(ampere::mesh, IOEntryState, Invalid, Executing, Blocked, Ready, Transmitting);

namespace ampere::mesh {

using namespace ampere::sc;

using IOEntryState = ampere::mesh::IOEntryState;

struct IOEntryObj : public Transaction
{
    IOEntryObj(uint64_t id, Type iotype, uint64_t tlp_size, PhysAddr address)
        : id{id}, iotype{iotype}, tlp_size{tlp_size}, address{address}
    {
        tracker_count = tlp_size;
    }

    IOEntryObj(const IOEntryObj&) = delete;

    using IOEntry = transaction_ptr<IOEntryObj>;

    uint64_t id;
    IOEntryState state{IOEntryState::Invalid};
    Type iotype;
    uint64_t tlp_size;
    PhysAddr address;
    int tracker_count;
    std::list<IOEntry> wake_list;
    IOEntry next;
    IOEntry prev;
    IOEntry next_w;
    IOEntry prev_w;
    int stream_id;
};

using IOEntry = transaction_ptr<IOEntryObj>;

struct HeadPtr
{
    IOEntry head{nullptr};
    IOEntry tail{nullptr};
};

class IOTracker : public Module
{
public:
    IOTracker(const sc_core::sc_module_name& name,
              unsigned num_sources,
              unsigned iord_tracker_size,
              unsigned iowr_tracker_size)
        : Module{name},
          num_sources{num_sources},
          iord_tracker_size{iord_tracker_size},
          iowr_tracker_size{iowr_tracker_size}
    {
        for (unsigned source = 0; source < num_sources; source++) {
            io_free_list[source].resize(iord_tracker_size + iowr_tracker_size); // for IO Tracker
            std::iota(io_free_list.at(source).begin(), io_free_list.at(source).end(), 0);
            iord_occ[source] = 0;
            iowr_occ[source] = 0;
        }
        io_ptr.resize(num_sources);
        io_wrptr.resize(num_sources);
    }

    unsigned get_iord_tracker_size(unsigned source) { return iord_occ.at(source); }
    unsigned get_iowr_tracker_size(unsigned source) { return iowr_occ.at(source); }
    bool is_iord_tracker_empty(unsigned source) { return iord_occ.at(source) == 0; }
    bool is_iowr_tracker_empty(unsigned source) { return iowr_occ.at(source) == 0; }
    unsigned get_num_sources() { return num_sources; }
    unsigned get_iord_tracker_size() { return iord_tracker_size; }
    unsigned get_iowr_tracker_size() { return iowr_tracker_size; }

    void incr_rd_occ(unsigned i) { iord_occ.at(i)++; }
    void incr_wr_occ(unsigned i) { iowr_occ.at(i)++; }

    void decr_rd_occ(unsigned i) { iord_occ.at(i)--; }
    void decr_wr_occ(unsigned i) { iowr_occ.at(i)--; }

    IOEntry get_next_ready_io(unsigned i, Type iotype)
    {
        IOEntry node = io_ptr.at(i).head;
        while (node != io_ptr.at(i).tail) {
            if (node->state == IOEntryState::Ready && node->iotype == iotype)
                return node;
            node = node->next;
        }
        if (node->state == IOEntryState::Ready && node->iotype == iotype)
            return node;

        return nullptr;
    }
    IOEntry get_last_wr_entry(unsigned i) { return io_wrptr.at(i).tail; }

    IOEntry insert_io(Type iotype, uint64_t tlp_size, PhysAddr address, unsigned i)
    {
        uint64_t n = io_free_list.at(i).front();
        io_free_list.at(i).pop_front();
        IOEntry node = make<IOEntry>(n, iotype, tlp_size, address);
        _insert(io_ptr.at(i), node);

        if (iotype == Type::Write) {
            _insert_wr(io_wrptr.at(i), node);
        }

        return node;
    }

    void _insert(HeadPtr& ptr, IOEntry node)
    {
        if (ptr.head == nullptr) {
            ptr.tail = node;
            ptr.head = node;
            node->next = node;
            node->prev = node;
        }

        ptr.tail = ptr.head->prev;
        ptr.tail->next = node;
        node->prev = ptr.tail;
        node->next = ptr.head;
        ptr.head->prev = node;
        ptr.tail = node;
    }

    void _insert_wr(HeadPtr& ptr, IOEntry node)
    {
        if (ptr.head == nullptr) {
            ptr.tail = node;
            ptr.head = node;
            node->next_w = node;
            node->prev_w = node;
        }

        ptr.tail = ptr.head->prev_w;
        ptr.tail->next_w = node;
        node->prev_w = ptr.tail;
        node->next_w = ptr.head;
        ptr.head->prev_w = node;
        ptr.tail = node;
    }

    IOEntry _search(HeadPtr& ptr, uint64_t n)
    {
        IOEntry node = ptr.head;
        while (node->next != ptr.head) {
            if (node->id == n)
                return node;
            node = node->next;
        }
        if (node->id == n)
            return node;
        return nullptr;
    }

    void delete_io(uint64_t n, unsigned i, Type iotype)
    {
        IOEntry node = _search(io_ptr.at(i), n);
        _remove(io_ptr.at(i), node);
        if (iotype == Type::Write)
            _remove_w(io_wrptr.at(i), node);

        io_free_list.at(i).push_back(n);
    }

    void _remove(HeadPtr& ptr, IOEntry node)
    {
        if (node == nullptr) {
            AMPERE_FATAL("IO entry {} not found in IO Tracker", node->id);
            return;
        }

        node->prev->next = node->next;
        node->next->prev = node->prev;

        if (node == ptr.head)
            ptr.head = node->next;
        else if (node == ptr.tail)
            ptr.tail = node->prev;
    }

    void _remove_w(HeadPtr& ptr, IOEntry node)
    {
        if (node == nullptr) {
            AMPERE_FATAL("IO entry {} not found in IO Tracker", node->id);
            return;
        }

        node->prev_w->next_w = node->next_w;
        node->next_w->prev_w = node->prev_w;

        if (node == ptr.head)
            ptr.head = node->next_w;
        else if (node == ptr.tail)
            ptr.tail = node->prev_w;
    }

private:
    std::map<int, std::deque<uint64_t>> io_free_list;
    std::map<int, unsigned> iord_occ;
    std::map<int, unsigned> iowr_occ;

    std::vector<HeadPtr> io_ptr;
    std::vector<HeadPtr> io_wrptr;

    unsigned num_sources;
    unsigned iord_tracker_size;
    unsigned iowr_tracker_size;
};

} // namespace ampere::mesh
