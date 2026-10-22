//
// Copyright (c) 2020-2022 Ampere Computing. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause
//
// The IDT component encapsulates RDI to reduce the Retry Ack loop. The port "a"/"b" connects
// either to IDT or RDI depending on whether IDT is enabled or not. The port "b_to_a_idt"
// (at the IDT side) is connected to "b_to_a_rdi" (at the RDI side), as shown by the link "b_to_a".
// The port "a_to_b_idt" (at the IDT side) is connected to "a_to_b_rdi" (at the RDI side),
// as shown by the link "a_to_b".
//
//         __________________                              ___________________
//        |                  |                            |                   |
//        | ||       ||      |________||       ||_________|       ||       || |
//   a____|_|| b_to_a||               ||       ||                 ||a_to_b ||_|_____b
//          ||  IDT  ||-----b_to_a----||  RDI  ||------a_to_b-----||  IDT  ||
//          ||       ||               ||       ||                 ||       ||
//          ||       ||               ||       ||                 ||       ||
//

#pragma once

#include <queue>

#include "ampere/mesh/chi/types.h"
#include "ampere/mesh/mesh_device.h"
#include "ampere/mesh/mesh_module.h"
#include "ampere/mesh/mesh_port.h"
#include "ampere/mesh/retry.h"
#include "ampere/mesh/rdi.h"
#include "ampere/mesh/traffic_buffer.h"
#include "ampere/sc/poly_constructors.h"
#include "ampere/sc/sc.h"
#include "ampere/mesh/cbusy.h"
#include "ampere/mesh/rcbusy.h"

namespace ampere::mesh {

using namespace chi;
using namespace sc;

template<typename T>
class Queue : std::deque<T>
{
public:
    Queue(size_t max_size) : max_size{max_size} {}

    bool full() { return size() == max_size; }
    void push_back(const T& x)
    {
        AMPERE_ASSERT(!full(), "cannot push to full Queue");
        std::deque<T>::push_back(x);
    }
    void push_back(T&& x)
    {
        AMPERE_ASSERT(!full(), "cannot push to full Queue");
        std::deque<T>::push_back(x);
    }
    using std::deque<T>::front;
    using std::deque<T>::pop_front;
    using std::deque<T>::size;
    using std::deque<T>::empty;

private:
    size_t max_size;
};

class IDTLink : public MeshModule, MeshEvents
{
public:
    IDTLink(const sc_core::sc_module_name& name, bool idm_mode);
    bool run() override;

    enum {
        Wait4RRorRSD = 0b0000001,
        Wait4DBID = 0b0000010,
        Wait4Comp = 0b0000100,
        Wait4Data = 0b0001000,
        Wait4CompAck = 0b0010000
    };

    Param<uint64_t> rtrk_size{"rtrk_size", "size of request tracker buffer", 256};

    uint8_t die_id{0};
    NodeId lrid{NodeId{0}};

    MeshPort rdi_port{"rdi_port"};
    MeshPort idt_port{"idt_port"};

    In<std::pair<int, int>> in_credit_return;
    Out<std::pair<int, int>> out_credit_return;

    bool full() const { return free_list.empty(); }
    bool empty() const { return free_list.size() == rtrk_size; }
    size_t occupancy() const { return rtrk_size - free_list.size(); }
    size_t free_list_size() const { return free_list.size(); }

    struct RTRKEntry
    {
        MeshReq req;
        uint64_t id;
        uint16_t dbid;
        NodeId homenid{0};
        uint8_t pcrdtype{0};
        bool retry{false};
        int wait{0};
        NodeId resp_src_id{0};
        NodeId data_src_id{0};
        CBusyState cbusy;

        RTRKEntry(uint64_t id) : id{id} { req = nullptr; }
    };

    // To be used by QOS
    /*struct PCTEntry {
        uint8_t seen_qpc;
        uint16_t count;
        uint8_t max_qpc_count;
    };*/

    // std::map<uint64_t, PCTEntry> PCT;

protected:
    template<typename T>
    bool transmit(TrafficBuffer<T>& q, OutOpt<T>& port, Event& send_event);
    void send_request(RTRKEntry& entry);
    void send_response(RTRKEntry& entry, MeshRsp& rsp);
    void send_response_to_completer(RTRKEntry& entry, MeshRsp& rsp);
    void send_data(RTRKEntry& entry, DataOpcode op, ResponseStatus resp);
    void send_data_to_completer(RTRKEntry& entry, DataOpcode op, ResponseStatus resp);
    void receive_requests_in_idtt(Event& recv_event);
    void receive_requests_in_idts(Event& recv_event);
    void receive_responses_from_completer(Event& recv_event);
    void receive_responses_from_requester(Event& recv_event);
    void receive_data_from_completer(Event& recv_event);
    void receive_data_from_requester(Event& recv_event);
    void transmit_from_idtt();
    void transmit_from_idts();
    void receive_rtrk_credits();

    // Retry Tracker
    Retry retry{"retry"};
    RetryRequester retry_requester{"retry_requester"};

    RCBusy rcbusy{"rcbusy"};
    CBusy cbusy{"cbusy"};
    unsigned int outstanding_requests{0};

    IDTLink::RTRKEntry* allocate_entry(MeshReq req);
    void deallocate_entry_if_not_waiting(RTRKEntry& entry);
    IDTLink::RTRKEntry* find_entry(uint64_t id);
    chi::CBusyState get_cbusy() { return cbusy.update_cbusy(occupancy()); }

    Event ev_retry_sent{"retry_sent", "A retry event occured"};
    Event ev_pcrd_sent{"pcrd_sent", "A PCrdGrant event occured"};
    Event ev_alloc{"alloc", "An IDT entry is allocated"};
    Event ev_dealloc{"dealloc", "An IDT entry is deallocated"};
    EventData<uint8_t> evd_occupancy{"occupancy", "IDT occupancy"};

private:
    bool idm_mode;

    std::vector<RTRKEntry> rtrk_q;
    std::deque<int> free_list;
    TrafficBuffer<MeshReq> mesh_req_q{"mesh_req_q"};   // queue for sending requests to Mesh
    TrafficBuffer<MeshRsp> mesh_resp_q{"mesh_resp_q"}; // queue for sending responses to Mesh
    TrafficBuffer<MeshDat> mesh_data_q{"mesh_data_q"}; // queue for sending data to Mesh
    TrafficBuffer<MeshReq> rdi_req_q{"rdi_req_q"};     // queue for sending requests to RDI
    TrafficBuffer<MeshRsp> rdi_resp_q{"rdi_resp_q"};   // queue for sending responses to RDI
    TrafficBuffer<MeshDat> rdi_data_q{"rdi_data_q"};   // queue for sending data to RDI

    CreditPool rdi_in_req_credits{"rdi_in_req"};
    CreditPool rdi_in_rsp_credits{"rdi_in_rsp"};
    CreditPool rdi_in_dat_credits{"rdi_in_dat"};

    CreditPool idt_in_req_credits{"idt_in_req"};
    CreditPool idt_in_rsp_credits{"idt_in_rsp"};
    CreditPool idt_in_dat_credits{"idt_in_dat"};
};

class IDT : public DieConnector, MeshEvents, poly<IDT, poly_constructor_default, poly_constructor_clock>
{
public:
    explicit IDT(const sc_core::sc_module_name& name) : IDT{name, nullptr} {}
    IDT(const sc_core::sc_module_name& name, sc::Clock* clock);

    void set_a_to_b_lrid(NodeId lrid, uint8_t die_id);
    void set_b_to_a_lrid(NodeId lrid, uint8_t die_id);

private:
    Param<bool> idm_mode{"idm_mode", "enables IDM mode", false};

    IDTLink b_to_a_idt{"b_to_a_idt", idm_mode};
    IDTLink a_to_b_idt{"a_to_b_idt", idm_mode};

    RDI rdi{"rdi"};
};

} // namespace ampere::mesh
