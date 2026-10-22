//
// Copyright (c) 2018-2019 Ampere Computing. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause
//
#pragma once

#include <sysc/communication/sc_port.h>
#include <optional>
#include <stdexcept>
#include <string>
#include <type_traits>
#include <utility>

#include "ampere/sc/ancestor.h"
#include "ampere/sc/module.h"
// IWYU pragma: begin_exports
#include "ampere/sc/connection.h"
#include "ampere/sc/fifo/cx_queue.h"
#include "ampere/sc/fifo/cx_queue_with_credit.h"
#include "ampere/sc/fifo/delay.h"
#include "ampere/sc/fifo/delay_bw.h"
#include "ampere/sc/fifo/delay_with_credit.h"
#include "ampere/sc/fifo/queue.h"
#include "ampere/sc/fifo/queue_bw.h"
#include "ampere/sc/fifo/queue_with_credit.h"
#include "ampere/sc/fifo/zdelay.h"
#include "ampere/sc/fifo/zdelay_with_credit.h"
#include "ampere/sc/fifo/zqueue.h"
#include "ampere/sc/fifo/zqueue_with_credit.h"
#include "ampere/sc/mimo/broadcast.h"
#include "ampere/sc/port_multiplex.h"
#include "ampere/sc/taker.h"
#include "ampere/sc/putter.h"
// IWYU pragma: end_exports

namespace ampere::sc {

struct ConnectionFactory;
class ConnectionSetup;

template<typename T, bool ZERO_DELAY_ALLOWED = false, bool OPTIONAL = false>
class OutIn : public Putter<T>, public Taker<T>
{
    std::string name;
    std::string out_name;
    std::string in_name;

    using out_t = std::conditional_t<OPTIONAL, OutOpt<T>, Out<T>>;
    using in_t = std::conditional_t<OPTIONAL, InOpt<T>, In<T>>;

public:
    template<typename U, bool Z, bool O>
    friend ConnectionSetup connect(OutIn<U, Z, O>&);

    out_t out;
    in_t in;

    explicit OutIn(const std::string& name)
        : name(name), out_name(name + "_out"), in_name(name + "_in"), out(out_name.c_str()), in(in_name.c_str())
    {
    }

    bool can_put() const override { return out->can_put(); }
    void put(T value) override { return out->put(value); }

    bool can_take() const override { return in->can_take(); }
    T take() override { return in->take(); }
    void clear() override { in->clear(); }
    void clear_if(const std::function<bool(const T&)>& pred) override { in->clear_if(pred); }

    const T& peek(uint64_t index = 0) const override { return in->peek(index); }
    uint64_t num_ready() const override { return in->num_ready(); }

    const T& unsafe_peek(uint64_t index = 0) const override { return in->unsafe_peek(index); }
    uint64_t unsafe_size() const override { return in->unsafe_size(); }

    const T& get(uint64_t index = 0) const override { return out->get(index); }
    uint64_t size() const override { return out->size(); }
};

template<typename T>
using ZeroDelayOutIn = OutIn<T, true>;

template<typename T>
using ZeroDelayOutInOpt = OutIn<T, true, true>;

template<typename T>
using OutInOpt = OutIn<T, false, true>;

class ConnectionSettings
{
protected:
    std::string m_name;
    CycleDelta m_default_latency{1};
    std::optional<CycleDelta> m_latency;
    std::optional<CycleDelta> m_return_latency;
    std::optional<size_t> m_capacity;
    size_t m_default_rd_bandwidth{1};
    size_t m_default_wr_bandwidth{1};
    std::optional<size_t> m_rd_bandwidth;
    std::optional<size_t> m_wr_bandwidth;
    std::optional<sc_core::sc_time> m_delay;
    std::optional<sc_core::sc_time> m_return_delay;
    std::optional<bool> m_lossy;
    bool m_lossy_allowed = false;
    bool m_allow_zero_delay_self_connection = false;

public:
    const char* get_name() const { return m_name.c_str(); }
    CycleDelta get_latency() const { return *m_latency; }
    CycleDelta get_return_latency() const { return *m_return_latency; }
    size_t get_capacity() const { return *m_capacity; }
    size_t get_rd_bandwidth() const { return *m_rd_bandwidth; }
    size_t get_wr_bandwidth() const { return *m_wr_bandwidth; }
    sc_core::sc_time get_delay() const { return *m_delay; }
    sc_core::sc_time get_return_delay() const { return *m_return_delay; }
    bool is_zero_cycle_self_connection_allowed() const { return m_allow_zero_delay_self_connection; }
    bool is_lossy() const { return m_lossy && *m_lossy; }
};

class ConnectionSetup : public ConnectionSettings
{
public:
    template<typename U, bool Z, bool O>
    friend ConnectionSetup connect(OutIn<U, Z, O>&);

    ConnectionSetup(Module* module, std::unique_ptr<ConnectionFactory> factory);
    ConnectionSetup(const ConnectionSetup&) = delete;
    ConnectionSetup(ConnectionSetup&&) = default;
    ~ConnectionSetup();

    ConnectionSettings bind();

    ConnectionSetup& name(std::string nm);
    ConnectionSetup& default_latency(CycleDelta lat);
    ConnectionSetup& latency(CycleDelta lat);
    ConnectionSetup& return_latency(CycleDelta ret_lat);
    ConnectionSetup& capacity(size_t cap);
    ConnectionSetup& default_bandwidth(size_t bw);
    ConnectionSetup& bandwidth(size_t bw);
    ConnectionSetup& rd_bandwidth(size_t bw);
    ConnectionSetup& wr_bandwidth(size_t bw);
    ConnectionSetup& delay(sc_core::sc_time d);
    ConnectionSetup& return_delay(sc_core::sc_time d);
    ConnectionSetup& potentially_lossy();
    ConnectionSetup& lossy();

    std::string get_local_name() const;

private:
    Module* module = nullptr;
    std::unique_ptr<ConnectionFactory> factory;

    void make_and_bind();
    void update_from_params();
    ParameterRegistry& get_param_registry();
    std::string get_full_name() const;
    ConnectionSetup& allow_zero_delay_self_connection();

    template<typename T>
    ParameterRegistry::Parameter* update_if_not_set(std::optional<T>* pvalue,
                                                    ParameterRegistry& registry,
                                                    const std::string& pname,
                                                    const std::string& description,
                                                    const json& default_value);
};

template<typename T>
struct PortSettings
{};

template<typename Ifc, sc_core::sc_port_policy Policy, int N>
struct PortSettings<sc_core::sc_port<Ifc, N, Policy>>
{
    static constexpr int n = N;
    static constexpr sc_core::sc_port_policy policy = Policy;
    constexpr explicit PortSettings(const sc_core::sc_port<Ifc, N, Policy>&) {}
};

struct ConnectionFactory
{
    virtual ~ConnectionFactory() = default;
    virtual bool forwards_from() const { return false; }
    virtual void forward() {}
    virtual void make(const ConnectionSetup& params) const = 0;
};

template<typename OUT, typename IN, typename T>
class FifoFactory final : public ConnectionFactory
{
public:
    FifoFactory(OUT& src, IN& tgt) : src{src}, tgt{tgt} {}

    bool forwards_from() const override
    {
        if (src.bind_count() > 0)
            return dynamic_cast<TakerMultiplexer<T, PortSettings<OUT>::policy>*>(src.get_interface());
        return false;
    }

    void forward() override
    {
        auto* fwd = dynamic_cast<TakerMultiplexer<T, PortSettings<OUT>::policy>*>(src.get_interface());
        tgt.bind(*fwd);
    }

    void make(const ConnectionSetup& params) const final
    {
        auto src_mod = get_nearest_ancestor<Module>(&src);
        auto tgt_mod = get_nearest_ancestor<Module>(&tgt);

        Clock& src_clock = *src_mod->get_clock();
        Clock& tgt_clock = *tgt_mod->get_clock();

        Fifo<T>* fifo = nullptr;

        auto fifo_name = params.get_local_name();
        if (&src_clock != &tgt_clock) {
            AMPERE_ASSERT(
                params.get_latency() == 1, "latency is not allowed for clock-crossing FIFO {}", params.get_name());
            AMPERE_ASSERT(
                params.get_capacity() > 0, "capacity > 0 is required for clock-crossing FIFO {}", params.get_name());
            fifo = new CxQueue<T>(fifo_name.c_str(),
                                  src_clock,
                                  tgt_clock,
                                  params.get_delay(),
                                  params.get_return_delay(),
                                  params.get_capacity());
        } else if (params.get_latency() > 0) {
            AMPERE_ASSERT(params.get_delay() == sc_core::SC_ZERO_TIME,
                          "delay is not allowed for non-clock-crossing FIFO {}",
                          params.get_name());
            if (params.get_capacity() > 0) {
                if (params.get_rd_bandwidth() > 1 || params.get_wr_bandwidth() > 1)
                    fifo = new QueueBW<T>(fifo_name.c_str(),
                                          tgt_clock,
                                          params.get_latency(),
                                          params.get_return_latency(),
                                          params.get_capacity(),
                                          params.get_rd_bandwidth(),
                                          params.get_wr_bandwidth(),
                                          params.is_lossy());
                else
                    fifo = new Queue<T>(fifo_name.c_str(),
                                        tgt_clock,
                                        params.get_latency(),
                                        params.get_return_latency(),
                                        params.get_capacity(),
                                        params.is_lossy());
            } else {
                AMPERE_ASSERT(
                    !params.is_lossy(), "lossy is not supported for zero capacity FIFO {}", params.get_name());
                if (params.get_rd_bandwidth() > 1)
                    fifo =
                        new DelayBW<T>(fifo_name.c_str(), tgt_clock, params.get_latency(), params.get_rd_bandwidth());
                else
                    fifo = new Delay<T>(fifo_name.c_str(), tgt_clock, params.get_latency());
            }
        } else {
            AMPERE_ASSERT(params.get_rd_bandwidth() == params.get_wr_bandwidth(),
                          "read/write bandwidth should be equal for zero delay FIFO {}",
                          params.get_name());
            AMPERE_ASSERT(!params.is_lossy(), "lossy is not supported for zero delay FIFO {}", params.get_name());
            if (params.get_capacity() == 0)
                fifo = new ZDelay<T>(fifo_name.c_str(),
                                     tgt_clock,
                                     params.get_rd_bandwidth(),
                                     params.is_zero_cycle_self_connection_allowed());
            else
                fifo = new ZQueue<T>(fifo_name.c_str(),
                                     tgt_clock,
                                     params.get_return_latency(),
                                     params.get_capacity(),
                                     params.get_rd_bandwidth(),
                                     params.is_zero_cycle_self_connection_allowed());
        }

        assert(fifo);

        src.bind(*fifo);
        tgt.bind(*fifo);
    }

private:
    OUT& src;
    IN& tgt;
};

namespace {

template<typename T, int N, sc_core::sc_port_policy P>
InWithCredit<T, N, P>* get_actual_port(sc_core::sc_port<TakerWithCredit<T>, N, P>& parent_port)
{
    auto p = dynamic_cast<InWithCredit<T, N, P>*>(&parent_port);
    AMPERE_ASSERT(p, "port is not an InWithCredit");
    return p;
}

} // namespace

template<typename OUT, typename IN, typename T>
class FifoWithCreditFactory final : public ConnectionFactory
{
public:
    FifoWithCreditFactory(OUT& src, IN& tgt) : src{src}, tgt{tgt} {}

    void make(const ConnectionSetup& params) const final
    {
        auto src_mod = get_nearest_ancestor<Module>(&src);
        auto tgt_mod = get_nearest_ancestor<Module>(&tgt);

        Clock& src_clock = *src_mod->get_clock();
        Clock& tgt_clock = *tgt_mod->get_clock();

        FifoWithCredit<T>* fifo = nullptr;

        AMPERE_ASSERT(params.get_rd_bandwidth() == 1 && params.get_wr_bandwidth() == 1,
                      "FifoWithCreditFactory does not currently support bandwidth > 1");

        auto fifo_name = params.get_local_name();
        if (&src_clock != &tgt_clock) {
            AMPERE_ASSERT(params.get_latency() == 1,
                          "cannot apply latency {} to clock crossing queue {}",
                          params.get_latency(),
                          params.get_name());
            fifo = new CxQueueWithCredit<T>(fifo_name.c_str(),
                                            src_clock,
                                            tgt_clock,
                                            params.get_delay(),
                                            params.get_return_delay(),
                                            params.get_capacity());
        } else if (params.get_latency() > 0) {
            if (params.get_capacity())
                fifo = new QueueWithCredit<T>(fifo_name.c_str(),
                                              tgt_clock,
                                              params.get_latency(),
                                              params.get_return_latency(),
                                              params.get_capacity());
            else
                fifo = new DelayWithCredit<T>(
                    fifo_name.c_str(), tgt_clock, params.get_latency(), params.get_return_latency());
        } else {
            if (params.get_capacity() == 0)
                fifo = new ZDelayWithCredit<T>(fifo_name.c_str(),
                                               tgt_clock,
                                               params.get_rd_bandwidth(),
                                               params.is_zero_cycle_self_connection_allowed());
            else
                fifo = new ZQueueWithCredit<T>(fifo_name.c_str(),
                                               tgt_clock,
                                               params.get_return_latency(),
                                               params.get_capacity(),
                                               params.get_rd_bandwidth(),
                                               params.is_zero_cycle_self_connection_allowed());
        }

        assert(fifo);

        fifo->add_target(tgt_mod, src_mod);

        src.bind(*fifo);
        tgt.bind(*fifo);
    }

private:
    OUT& src;
    IN& tgt;
};

template<typename OUT, typename READ, typename T>
class MimoFactory final : public ConnectionFactory
{
public:
    MimoFactory(OUT& src, std::initializer_list<READ*> tgts) : src{src}, tgts(tgts) {}

    void make(const ConnectionSetup& params) const final
    {
        auto src_mod = get_nearest_ancestor<Module>(&src);

        Clock& clock = *src_mod->get_clock();

        auto fifo_name = params.get_local_name();
        Mimo<T>* mimo = new Broadcast<T>(fifo_name.c_str(), clock, params.get_latency());

        assert(mimo);

        src.bind(*mimo);

        for (auto& tgt : tgts) {
            auto tgt_mod = get_nearest_ancestor<Module>(tgt);
            mimo->add_target(tgt_mod, src_mod);
            tgt->bind(*mimo);
        }
    }

private:
    OUT& src;
    std::vector<READ*> tgts;
};

template<typename WrIfc, typename WrPortType, typename RdIfc, typename RdPortType>
struct ConnectionFactorySelector
{
    using type = std::false_type;
};

template<typename T, typename WrPortType, typename RdPortType>
struct ConnectionFactorySelector<Putter<T>, WrPortType, Taker<T>, RdPortType>
{
    using type = FifoFactory<WrPortType, RdPortType, T>;
};

template<typename T, typename WrPortType, typename RdPortType>
struct ConnectionFactorySelector<Putter<T>, WrPortType, Reader<T>, RdPortType>
{
    using type = MimoFactory<WrPortType, RdPortType, T>;
};

template<typename T, typename WrPortType, typename RdPortType>
struct ConnectionFactorySelector<Putter<T>, WrPortType, TakerWithCredit<T>, RdPortType>
{
    using type = FifoWithCreditFactory<WrPortType, RdPortType, T>;
};

// Connect two ports within current module
template<typename WrIfc,
         sc_core::sc_port_policy WrPolicy = sc_core::SC_ONE_OR_MORE_BOUND,
         int WrN = 1,
         typename RdIfc,
         sc_core::sc_port_policy RdPolicy = sc_core::SC_ONE_OR_MORE_BOUND,
         int RdN = 1>
ConnectionSetup connect(sc_core::sc_port<WrIfc, WrN, WrPolicy>& src, sc_core::sc_port<RdIfc, RdN, RdPolicy>& tgt)
{
    using factory_type = typename ConnectionFactorySelector<WrIfc,
                                                            sc_core::sc_port<WrIfc, WrN, WrPolicy>,
                                                            RdIfc,
                                                            sc_core::sc_port<RdIfc, RdN, RdPolicy>>::type;

    static_assert(!std::is_same_v<factory_type, std::false_type>,
                  "unsupported write or read interfaces passed to connect");

    auto factory = std::make_unique<factory_type>(src, tgt);
    return ConnectionSetup{get_nearest<Module>(), std::move(factory)};
}

// Connect port to multiple readers within current module
template<typename WrIfc,
         sc_core::sc_port_policy WrPolicy = sc_core::SC_ONE_OR_MORE_BOUND,
         int WrN = 1,
         typename RdIfc,
         sc_core::sc_port_policy RdPolicy = sc_core::SC_ONE_OR_MORE_BOUND,
         int RdN = 1>
ConnectionSetup connect(sc_core::sc_port<WrIfc, WrN, WrPolicy>& src,
                        std::initializer_list<sc_core::sc_port<RdIfc, RdN, RdPolicy>*> tgts)
{
    using factory_type = typename ConnectionFactorySelector<WrIfc,
                                                            sc_core::sc_port<WrIfc, WrN, WrPolicy>,
                                                            RdIfc,
                                                            sc_core::sc_port<RdIfc, RdN, RdPolicy>>::type;

    static_assert(!std::is_same_v<factory_type, std::false_type>,
                  "unsupported write or read interfaces passed to connect");

    auto factory = std::make_unique<factory_type>(src, tgts);
    return ConnectionSetup{get_nearest<Module>(), std::move(factory)};
}

// Connect two ports within the source module
template<typename WrIfc,
         sc_core::sc_port_policy WrPolicy = sc_core::SC_ONE_OR_MORE_BOUND,
         int WrN = 1,
         typename RdIfc,
         sc_core::sc_port_policy RdPolicy = sc_core::SC_ONE_OR_MORE_BOUND,
         int RdN = 1>
ConnectionSetup connect_from(sc_core::sc_port<WrIfc, WrN, WrPolicy>& src, sc_core::sc_port<RdIfc, RdN, RdPolicy>& tgt)
{
    using factory_type = typename ConnectionFactorySelector<WrIfc,
                                                            sc_core::sc_port<WrIfc, WrN, WrPolicy>,
                                                            RdIfc,
                                                            sc_core::sc_port<RdIfc, RdN, RdPolicy>>::type;

    static_assert(!std::is_same_v<factory_type, std::false_type>,
                  "unsupported write or read interfaces passed to connect");

    auto factory = std::make_unique<factory_type>(src, tgt);
    return ConnectionSetup{get_nearest<Module>(&src), std::move(factory)};
}

// Connect port to multiple readers within source module
template<typename WrIfc,
         sc_core::sc_port_policy WrPolicy = sc_core::SC_ONE_OR_MORE_BOUND,
         int WrN = 1,
         typename RdIfc,
         sc_core::sc_port_policy RdPolicy = sc_core::SC_ONE_OR_MORE_BOUND,
         int RdN = 1>
ConnectionSetup connect_from(sc_core::sc_port<WrIfc, WrN, WrPolicy>& src,
                             std::initializer_list<sc_core::sc_port<RdIfc, RdN, RdPolicy>*> tgts)
{
    using factory_type = typename ConnectionFactorySelector<WrIfc,
                                                            sc_core::sc_port<WrIfc, WrN, WrPolicy>,
                                                            RdIfc,
                                                            sc_core::sc_port<RdIfc, RdN, RdPolicy>>::type;

    static_assert(!std::is_same_v<factory_type, std::false_type>,
                  "unsupported write or read interfaces passed to connect");

    auto factory = std::make_unique<factory_type>(src, tgts);
    return ConnectionSetup{get_nearest<Module>(&src), std::move(factory)};
}

// Connect two ports within the target module
template<typename WrIfc,
         sc_core::sc_port_policy WrPolicy = sc_core::SC_ONE_OR_MORE_BOUND,
         int WrN = 1,
         typename RdIfc,
         sc_core::sc_port_policy RdPolicy = sc_core::SC_ONE_OR_MORE_BOUND,
         int RdN = 1>
ConnectionSetup connect_to(sc_core::sc_port<WrIfc, WrN, WrPolicy>& src, sc_core::sc_port<RdIfc, RdN, RdPolicy>& tgt)
{
    using factory_type = typename ConnectionFactorySelector<WrIfc,
                                                            sc_core::sc_port<WrIfc, WrN, WrPolicy>,
                                                            RdIfc,
                                                            sc_core::sc_port<RdIfc, RdN, RdPolicy>>::type;

    static_assert(!std::is_same_v<factory_type, std::false_type>,
                  "unsupported write or read interfaces passed to connect");

    auto factory = std::make_unique<factory_type>(src, tgt);
    return ConnectionSetup{get_nearest<Module>(&tgt), std::move(factory)};
}

// Connect self-reading/writing port
template<typename T, bool ZERO_DELAY, bool OPTIONAL>
ConnectionSetup connect(OutIn<T, ZERO_DELAY, OPTIONAL>& src_tgt)
{
    auto connection = connect(src_tgt.out, src_tgt.in);
    connection.name(src_tgt.name);
    if constexpr (ZERO_DELAY) {
        connection.allow_zero_delay_self_connection();
    }
    return connection;
}

template<typename T, sc_core::sc_port_policy Policy = sc_core::SC_ONE_OR_MORE_BOUND>
auto multiplex(const char* name, sc_core::sc_port<Putter<T>, 1, Policy>& tgt)
{
    return typename TakerMultiplexer<T, Policy>::Setup(name, tgt);
}

} // namespace ampere::sc

/*
 * vim: sw=4:ts=4:et:si:cindent:cino=\:0,(0,g0,t0:
 */
