//
// Copyright (c) 2022 Ampere Computing. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause
//

#pragma once

#include <deque>
#include <map>
#include <string>
#include <vector>

#include "ampere/mesh/mesh_device.h"
#include "ampere/mesh/mesh_module.h"
#include "ampere/mesh/mesh_port.h"
#include "ampere/sc/poly_constructors.h"

namespace ampere::mesh {
namespace cpx {

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

template<typename T>
using CQueue = std::deque<std::pair<T, Credit>>;

class Device : public MeshModule
{
public:
    Device(const sc_core::sc_module_name& name, int nrsp);

private:
    void run_device();

    MeshPort port;

    Param<bool> matrix_rsp_routing{"matrix_rsp_routing", "Use Matrix rules to route responses on channel 0/1", false};

    Param<size_t> req_out_size{"req_out_size", "size of request output FIFO leading to devices", 4};
    Param<size_t> rsp_out_size{"rsp_out_size", "size of response output FIFO leading to devices", 4};
    Param<size_t> snp_out_size{"snp_out_size", "size of snoop output FIFO leading to devices", 4};
    Param<size_t> dat_out_size{"dat_out_size", "size of data output FIFO leading to devices", 4};

    CQueue<MeshReq> req_in;
    CQueue<MeshRsp> rsp0_in;
    CQueue<MeshRsp> rsp1_in;
    CQueue<MeshSnp> snp_in;
    CQueue<MeshDat> dat_in;

    Queue<MeshReq> req_out{req_out_size};
    Queue<MeshRsp> rsp0_out{rsp_out_size};
    Queue<MeshRsp> rsp1_out{rsp_out_size};
    Queue<MeshSnp> snp_out{snp_out_size};
    Queue<MeshDat> dat_out{dat_out_size};

    bool rsp_toggle{false};

    std::map<std::string, std::unique_ptr<CreditPool>> credit_pools;

    void make_credit_pool(const char* name);

    void end_of_elaboration() override;
    bool all_queues_empty() const;
    void receive_from_device();
    void transmit_to_device();

    CQueue<MeshRsp>& choose_rsp();

    friend class CPX;
};

/** CHI Port eXpander.
 *
 *  A device for muxing/demuxing CHI packets to/from multiple devices into a single CHI
 *  port that connects to a mesh.
 */
class CPX : public MeshModule,
            public implements<CPX, MeshDevice, sc::poly_constructor_default, sc::poly_constructor_clock>
{
public:
    explicit CPX(const sc_core::sc_module_name& name);
    CPX(const sc_core::sc_module_name& name, Clock* clock);

    Param<size_t> n{"n", "number of devices to be muxed", 2};
    Param<int> nrsp{"nrsp", "number of response channels for devices", 1};

    MeshPort mesh_port{"mesh_port", 1, 2, 1, 1};
    sc_core::sc_vector<MeshPort> device_port{"device_port"};

    bool run() override;
    void end_of_elaboration() override;

    MeshPort& get_mesh_port(int i = 0) override { throw std::runtime_error{"no mesh port"}; }
    NodeId get_node_id(int i = 0) const override { return NodeId{}; }

    void dump(std::ostream& os) const override;

private:
    sc_core::sc_vector<Device> device{"dev"};

    CQueue<MeshReq> mesh_req;
    CQueue<MeshRsp> mesh_rsp0;
    CQueue<MeshRsp> mesh_rsp1;
    CQueue<MeshSnp> mesh_snp;
    CQueue<MeshDat> mesh_dat;

    InWithCreditOpt<MeshReq>& mesh_req_credit_return;
    InWithCreditOpt<MeshRsp>& mesh_rsp0_credit_return;
    InWithCreditOpt<MeshRsp>& mesh_rsp1_credit_return;
    InWithCreditOpt<MeshSnp>& mesh_snp_credit_return;
    InWithCreditOpt<MeshDat>& mesh_dat_credit_return;

    CreditPool mesh_req_in{"mesh_req_in"};
    CreditPool mesh_rsp0_in{"mesh_rsp0_in"};
    CreditPool mesh_rsp1_in{"mesh_rsp1_in"};
    CreditPool mesh_snp_in{"mesh_snp_in"};
    CreditPool mesh_dat_in{"mesh_dat_in"};

    std::vector<CQueue<MeshReq>*> dev_req_in;
    std::vector<CQueue<MeshRsp>*> dev_rsp0_in;
    std::vector<CQueue<MeshRsp>*> dev_rsp1_in;
    std::vector<CQueue<MeshSnp>*> dev_snp_in;
    std::vector<CQueue<MeshDat>*> dev_dat_in;

    std::vector<InWithCreditOpt<MeshReq>*> dev_req_credit_return;
    std::vector<InWithCreditOpt<MeshRsp>*> dev_rsp0_credit_return;
    std::vector<InWithCreditOpt<MeshRsp>*> dev_rsp1_credit_return;
    std::vector<InWithCreditOpt<MeshSnp>*> dev_snp_credit_return;
    std::vector<InWithCreditOpt<MeshDat>*> dev_dat_credit_return;

    std::vector<Queue<MeshReq>*> dev_req_out;
    std::vector<Queue<MeshRsp>*> dev_rsp0_out;
    std::vector<Queue<MeshRsp>*> dev_rsp1_out;
    std::vector<Queue<MeshSnp>*> dev_snp_out;
    std::vector<Queue<MeshDat>*> dev_dat_out;

    std::vector<int> pri_req;
    std::vector<int> pri_rsp0;
    std::vector<int> pri_rsp1;
    std::vector<int> pri_snp;
    std::vector<int> pri_dat;

    void initialize();

    bool have_work() const;
    bool all_queues_empty() const;
    void transmit_to_mesh();
    void receive_from_mesh();
    void steer();

    template<typename T>
    void transmit_channel(std::vector<CQueue<T>*>& q,
                          OutOpt<T>& port,
                          std::vector<int>& pri,
                          std::vector<InWithCreditOpt<T>*>& credit_return);
    template<typename T>
    void steer_channel(CQueue<T>& mesh_q, std::vector<Queue<T>*>& dev_q, InWithCreditOpt<T>& credit_return);

    template<typename T>
    void dump_cqueue(const std::vector<CQueue<T>*>& cq, const char* name) const;
};

} // namespace cpx

using cpx::CPX;

} // namespace ampere::mesh
