//
// Copyright (c) 2023 Ampere Computing. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause
//
#pragma once

#include <map>
#include <queue>
#include <stdexcept>
#include <variant>
#include <vector>

#include "ampere/mesh/mesh_device.h"
#include "ampere/mesh/mesh_module.h"
#include "ampere/mesh/mesh_req.h"
#include "ampere/mesh/mesh_rsp.h"
#include "ampere/mesh/mesh_snp.h"
#include "ampere/mesh/mesh_dat.h"
#include "ampere/mesh/rdi.h"
#include "ampere/sc/poly_constructors.h"
#include "ampere/sc/sc.h"

namespace ampere::mesh {

using namespace sc;
using namespace sc_core;

template<int N>
class WeightedRRArbiter
{
public:
    WeightedRRArbiter()
    {
        std::fill(consecutive_grants.begin(), consecutive_grants.end(), 0);
        std::fill(weight.begin(), weight.end(), 1); // FIXME: parametrize
    }

    template<typename BidFunction>
    auto select(BidFunction bid) -> decltype(bid(0))
    {
        using return_type = decltype(bid(0));
        return_type grant{};

        int idx = last_idx;

        while (true) {
            if (grant = bid(idx); grant && consecutive_grants[idx] < weight[idx]) {
                consecutive_grants[idx]++;
                break;
            }

            consecutive_grants[idx] = 0;
            if (++idx == N) {
                idx = 0;
            }

            if (idx == last_idx)
                break;
        }

        last_idx = idx;
        return grant;
    }

    int get_last_idx() { return last_idx; }

private:
    int last_idx{0};
    std::array<int, N> consecutive_grants;
    std::array<int, N> weight;
};

/** Model of the Ampere Die Interconnect (ADI).
 *
 *  ADI connects CHI ports across dies (real ADI also handles AXI, but we only do CHI). Roughly
 *  speaking, an ADI is a set of input FIFOs for each CHI channel, a set of physical links of
 *  a different number than the CHI channels, and a set of output FIFOs for each CHI channel.
 *
 *  The physical link is modeled with a time-based delay, which is easier than trying to model
 *  the couple of clock crossings involved in the real PHY. Other than shuffling data from queue
 *  to queue, the meat of the work here is modeling the packing and unpacking of the CHI packets
 *  into the FLITs that traverse the physical link. Packing is specified with a set of rules
 *  describing how various FLIT formats may be built out of CHI packets. See `rdi_packing.h` for
 *  details.
 *
 *  ADI is a bi-directional connection and we've arbitrarily labeled one side "A" and the other
 *  "B" for lack of anything better. The two sides are aware of each other in order to handle
 *  credit returns (and, in reality, FLIT replay, but we don't model that). That's why this
 *  object is a giant container for all queues in both directions: an earlier implementation
 *  was built on separate templated models for each CHI packet type, but that fell short of
 *  reality in a couple of ways.
 */
class ADI : public DieConnector, poly<ADI, poly_constructor_default, poly_constructor_clock>
{
public:
    explicit ADI(const sc_module_name& name) : ADI{name, nullptr} {}
    ADI(const sc_module_name& name, Clock* clock);

    enum Channel {
        REQ,
        RSP0,
        RSP1,
        SNP,
        DAT
    };
    static constexpr int NUM_CHAN = 5;

    InOpt<std::pair<int, int>> a_in_aux_credit;
    InOpt<std::pair<int, int>> b_in_aux_credit;
    OutOpt<std::pair<int, int>> a_out_aux_credit;
    OutOpt<std::pair<int, int>> b_out_aux_credit;

    Param<size_t> num_links{"links", "Number of phyiscal links", 1};
    Param<sc_time> delay{"delay", "Physical link delay (default: 10ns)", sc_time{10, SC_NS}};
    Param<unsigned> flits_per_dat_pkt{"flits_per_dat_pkt", "How many FLITs a CHI data packet requires", 3};

    Param<size_t> a_out_req_fifo_size{"a_out_req_fifo_size", "size of a-to-b request output (shim) FIFO", 8};
    Param<size_t> a_out_rsp0_fifo_size{"a_out_rsp0_fifo_size", "size of a-to-b response 0 output (shim) FIFO", 8};
    Param<size_t> a_out_rsp1_fifo_size{"a_out_rsp1_fifo_size", "size of a-to-b response 1 output (shim) FIFO", 8};
    Param<size_t> a_out_snp_fifo_size{"a_out_snp_fifo_size", "size of a-to-b snoop output (shim) FIFO", 8};
    Param<size_t> a_out_dat_fifo_size{"a_out_dat_fifo_size", "size of a-to-b data output (shim) FIFO", 8};

    Param<size_t> b_out_req_fifo_size{"b_out_req_fifo_size", "size of b-to-a request output (shim) FIFO", 8};
    Param<size_t> b_out_rsp0_fifo_size{"b_out_rsp0_fifo_size", "size of b-to-a response 0 output (shim) FIFO", 8};
    Param<size_t> b_out_rsp1_fifo_size{"b_out_rsp1_fifo_size", "size of b-to-a response 1 output (shim) FIFO", 8};
    Param<size_t> b_out_snp_fifo_size{"b_out_snp_fifo_size", "size of b-to-a snoop output (shim) FIFO", 8};
    Param<size_t> b_out_dat_fifo_size{"b_out_dat_fifo_size", "size of b-to-a data output (shim) FIFO", 8};

    bool run() override;
    void end_of_elaboration() override;

protected:
    Event ev_a_out_req_fifo_out_of_credits{"a_out_req_fifo_out_of_credits",
                                           "can't send a request because the output FIFO has no credits"};
    Event ev_a_out_rsp0_fifo_out_of_credits{"a_out_rsp0_fifo_out_of_credits",
                                            "can't send a response because the output FIFO has no credits"};
    Event ev_a_out_rsp1_fifo_out_of_credits{"a_out_rsp1_fifo_out_of_credits",
                                            "can't send a response because the output FIFO has no credits"};
    Event ev_a_out_snp_fifo_out_of_credits{"a_out_snp_fifo_out_of_credits",
                                           "can't send a snoop because the output FIFO has no credits"};
    Event ev_a_out_dat_fifo_out_of_credits{"a_out_dat_fifo_out_of_credits",
                                           "can't send a data packet because the output FIFO has no credits"};

    Event ev_a_out_req_fifo_acquire{"a_out_req_fifo_acquire",
                                    "can't send a request because the output FIFO has no credits"};
    Event ev_a_out_rsp0_fifo_acquire{"a_out_rsp0_fifo_acquire",
                                     "can't send a response because the output FIFO has no credits"};
    Event ev_a_out_rsp1_fifo_acquire{"a_out_rsp1_fifo_acquire",
                                     "can't send a response because the output FIFO has no credits"};
    Event ev_a_out_snp_fifo_acquire{"a_out_snp_fifo_acquire",
                                    "can't send a snoop because the output FIFO has no credits"};
    Event ev_a_out_dat_fifo_acquire{"a_out_dat_fifo_acquire",
                                    "can't send a data packet because the output FIFO has no credits"};

    Event ev_b_out_req_fifo_out_of_credits{"b_out_req_fifo_out_of_credits",
                                           "can't send a request because the output FIFO has no credits"};
    Event ev_b_out_rsp0_fifo_out_of_credits{"b_out_rsp0_fifo_out_of_credits",
                                            "can't send a response because the output FIFO has no credits"};
    Event ev_b_out_rsp1_fifo_out_of_credits{"b_out_rsp1_fifo_out_of_credits",
                                            "can't send a response because the output FIFO has no credits"};
    Event ev_b_out_snp_fifo_out_of_credits{"b_out_snp_fifo_out_of_credits",
                                           "can't send a snoop because the output FIFO has no credits"};
    Event ev_b_out_dat_fifo_out_of_credits{"b_out_dat_fifo_out_of_credits",
                                           "can't send a data packet because the output FIFO has no credits"};

    struct EventContainer : public sc_core::sc_module, public MeshEvents
    {
        Event ev_pack{"pack", "How CHI packets were packed into FLITs"};
        Event ev_send_link{"send_link", "Send packet on link"};
        Event ev_double_slot{"double_slot", "Packed in double slot"};
        EventData<int> evd_fifo_size{"fifo_size", "Fifo size"};
        EventData<int> evd_pack_reqs{"pack_reqs", "Number of requests packed into this FLIT"};
        EventData<int> evd_pack_snps{"pack_snps", "Number of snoops packed into this FLIT"};
        EventData<int> evd_pack_dathdrs{"pack_dathdrs", "Number of data headers packed into this FLIT"};
        EventData<int> evd_pack_dats{"pack_dats", "Number of data packets packed into this FLIT"};
        EventData<int> evd_pack_rsp0{"pack_rsp0", "Number of response 0 packets packed into this FLIT"};
        EventData<int> evd_pack_rsp1{"pack_rsp1", "Number of response 1 packets packed into this FLIT"};

        EventContainer(const sc_core::sc_module_name& name) : sc_core::sc_module{name} {}
    };

    // These modules exist solely as a namespace for events so that we can distinguish between,
    // for example, recv_req going to A -> B and recv_req going from B -> A
    EventContainer a_to_b{"a_to_b"};
    EventContainer b_to_a{"b_to_a"};

    struct OutOfCreditEvents
    {
        Event* out_req_fifo_out_of_credits;
        Event* out_rsp0_fifo_out_of_credits;
        Event* out_rsp1_fifo_out_of_credits;
        Event* out_snp_fifo_out_of_credits;
        Event* out_dat_fifo_out_of_credits;
    };

    OutOfCreditEvents a_out_of_credits;
    OutOfCreditEvents b_out_of_credits;

    template<typename T>
    class InFifo : public Module
    {
    public:
        explicit InFifo(const sc_module_name& name) : Module{name} {}
        CreditPool credit_pool{"fifo"};

        struct Entry
        {
            T data;
            Credit credit;

            Entry(const T& data, Credit&& credit) : data{data}, credit{std::move(credit)} {}
        };

        void push(const T& data, Credit&& credit) { queue.emplace(data, std::move(credit)); }

        Entry& front() { return queue.front(); }
        const Entry& front() const { return queue.front(); }
        void push(T&& data) { queue.push(std::forward(data)); }
        void pop() { queue.pop(); }
        bool empty() const { return queue.empty(); }
        size_t size() const { return queue.size(); }

    private:
        std::queue<Entry> queue;
    };

    //
    // No, not real FLIT, but a "FLIT-let" because it only carries one CHI packet. A
    // full FLIT is modeled by several Flitlets stuffed into the same link with the
    // same `ready_time`.
    //
    struct Flitlet
    {
        int tag;
        sc_time ready_time;
        std::variant<MeshReq, MeshRsp, MeshSnp, MeshDat, int> payload;

        template<typename T>
        Flitlet(int tag, sc_time ready_time, T& payload) : tag{tag}, ready_time{ready_time}, payload{payload}
        {
        }
    };

    using Link = std::queue<Flitlet>;

    template<typename T>
    using OutFifo = std::queue<T>;

    struct InFifos
    {
        InFifos(const char* pfx)
            : req{fmt::format("{}_in_req", pfx).c_str()},
              rsp0{fmt::format("{}_in_rsp0", pfx).c_str()},
              rsp1{fmt::format("{}_in_rsp1", pfx).c_str()},
              snp{fmt::format("{}_in_snp", pfx).c_str()},
              dat{fmt::format("{}_in_dat", pfx).c_str()}
        {
        }

        InFifo<MeshReq> req;
        InFifo<MeshRsp> rsp0;
        InFifo<MeshRsp> rsp1;
        InFifo<MeshSnp> snp;
        InFifo<MeshDat> dat;
    };

    struct OutFifos
    {
        OutFifo<MeshReq> req;
        OutFifo<MeshRsp> rsp0;
        OutFifo<MeshRsp> rsp1;
        OutFifo<MeshSnp> snp;
        OutFifo<MeshDat> dat;
    };

    InFifos a_in{"a"};
    InFifos b_in{"b"};

    std::vector<Link> a_to_b_links{num_links};
    std::vector<Link> b_to_a_links{num_links};

    int a_to_b_in_flight_data;
    int b_to_a_in_flight_data;

    OutFifos b_out;
    OutFifos a_out;

    struct SlotContents
    {
        int req;
        int snp;
        int dat;
        int rsp0;
        int rsp1;

        friend SlotContents operator+(const SlotContents& a, const SlotContents& b)
        {
            return SlotContents{a.req + b.req, a.snp + b.snp, a.dat + b.dat, a.rsp0 + b.rsp0, a.rsp1 + b.rsp1};
        }
        friend std::ostream& operator<<(std::ostream& os, const SlotContents& a)
        {
            fmt::print(os, "[{},{},{},{},{}]", a.req, a.snp, a.dat, a.rsp0, a.rsp1);
            return os;
        }
    };

    static constexpr SlotContents slot0_slot1_packs[][2] = {
        {{.req = 1}, {.req = 1}}, {{.snp = 1}, {.snp = 1}}, {{.dat = 1}, {.dat = 1}}, {{.rsp0 = 2}, {.rsp1 = 2}}};

    static constexpr SlotContents slot0_packs[] = {{.req = 1}, {.snp = 1}, {.rsp0 = 2}};
    static constexpr SlotContents slot1_packs[] = {{.dat = 1}, {.rsp1 = 2}};

    static constexpr SlotContents slot2_packs[] = {
        {.req = 1}, {.snp = 1}, {.dat = 1}, {.rsp0 = 2}, {.rsp1 = 2}, {.rsp0 = 1, .rsp1 = 1}};

    static constexpr int slot0_slot1_packs_size = sizeof(slot0_slot1_packs) / sizeof(slot0_slot1_packs[0]);
    static constexpr int slot0_packs_size = sizeof(slot0_packs) / sizeof(slot0_packs[0]);
    static constexpr int slot1_packs_size = sizeof(slot1_packs) / sizeof(slot1_packs[0]);
    static constexpr int slot2_packs_size = sizeof(slot2_packs) / sizeof(slot2_packs[0]);

    struct WeightedRRArbiters
    {
        WeightedRRArbiter<slot0_slot1_packs_size> slot0_slot1_arb;
        WeightedRRArbiter<slot0_packs_size> slot0_arb;
        WeightedRRArbiter<slot1_packs_size> slot1_arb;
        WeightedRRArbiter<slot2_packs_size> slot2_arb;
    };

    WeightedRRArbiters a_to_b_arbiters;
    WeightedRRArbiters b_to_a_arbiters;

    uint64_t clock_period{get_clock()->get_period().value()};

    bool work_to_do() const;

    void receive();
    void transmit();

    template<typename T>
    void receive_from(InWithCreditOpt<T>& port, InFifo<T>& fifo, EventContainer& ev);
    template<typename T>
    void transmit_to(OutOpt<T>& port, OutFifo<T>& fifo, int& credits, EventContainer& ev);

    using Credits = std::unordered_map<int, int>;

    Credits a_to_b_credits;
    Credits b_to_a_credits;

    Credits a_to_b_return_credits;
    Credits b_to_a_return_credits;

    void pack_and_send(InFifos& fifos,
                       MeshPort& port,
                       Credits& credits,
                       Credits& return_credits,
                       int& in_flight_data,
                       std::vector<Link>& link,
                       OutOfCreditEvents& ooc,
                       WeightedRRArbiters& arb,
                       EventContainer& ev);

    std::optional<SlotContents> double_slot_bid(const InFifos& fifos,
                                                const SlotContents* pack,
                                                const Credits& send_credits) const;
    std::optional<SlotContents> single_slot_bid(const InFifos& fifos,
                                                const SlotContents& pack,
                                                const Credits& send_credits) const;
    std::optional<std::pair<SlotContents, bool>> special_slot2_bid(const InFifos& fifos,
                                                                   const SlotContents& pack,
                                                                   const Credits& send_credits) const;

    void send_return_credits(Channel chan, Credits& return_credits, sc_core::sc_time ready, std::vector<Link>& links);
    template<typename T>
    void send_packet_on_link(
        InFifo<T>& fifo, InWithCreditOpt<T>& port, int tag, Link& link, sc_time ready, EventContainer& ev);
    void recv_and_unpack(std::vector<Link>& links,
                         OutFifos& fifos,
                         Credits& credits,
                         OutOpt<std::pair<int, int>>& out_aux_credit);

    static bool is_credit(int tag) { return tag >= NUM_CHAN; }
    static bool is_external_credit(int tag) { return tag >= NUM_CHAN * 2; }
    static int decode_credit(int tag) { return tag - NUM_CHAN; }
    static int encode_external_credit(int tag) { return tag + NUM_CHAN * 2; }
    static int decode_external_credit(int tag) { return tag - NUM_CHAN * 2; }
};

} // namespace ampere::mesh
