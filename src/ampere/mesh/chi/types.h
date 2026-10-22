//
// Copyright (c) 2020 Ampere Computing. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause
//
#ifndef AMPERE_MESH_CHI_TYPES_H
#define AMPERE_MESH_CHI_TYPES_H

#include <fmt/core.h>
#include <fmt/ostream.h>
#include <cstdint>
#include <iostream>

#include "ampere/archx/addr.h"
#include "ampere/archx/bitfield.h"
#include "ampere/mesh/chi/opcodes.enum.h"
#include "ampere/sc/param.h"
#include "ampere/sc/report.h"
#include "ampere/sc/transaction.h"

namespace ampere::mesh {
namespace chi {

using namespace ampere;
using namespace ampere::archx;
using namespace ampere::sc;

struct InvalidResponseState : public std::runtime_error
{
    using std::runtime_error::runtime_error;
};

struct ResponseStatus
{
public:
    enum class Type {
        None,
        Snoop,
        Comp,
        CompWrNoSnpDef,
        WriteData,
        CompAck,
        TagMatch
    };

    ResponseStatus(const ResponseStatus&) = default;

    ResponseStatus() : m_type{Type::None}, m_encoding{0} {}
    ResponseStatus(Type type, uint8_t encoding) : m_type{type}, m_encoding{encoding} {}

    ResponseStatus(Type type, chi::FinalState state, bool pass_dirty) : m_type{type}
    {
        AMPERE_ASSERT(type == Type::Snoop,
                      "ResponseStatus constructor with pass_dirty is intended for Snoop type only");

        switch (state) {
        case chi::FinalState::I:
            m_encoding = 0b00;
            break;
        case chi::FinalState::SC:
            m_encoding = 0b01;
            break;
        case chi::FinalState::UC:
            m_encoding = 0b10;
            break;
        case chi::FinalState::UD:
            m_encoding = 0b10;
            break;
        case chi::FinalState::SD:
            m_encoding = 0b11;
            break;
        }

        if (pass_dirty)
            m_encoding |= 0b100;

        AMPERE_ASSERT(m_encoding != 0b111, "ResponseStatus: SD_PD is illegal for snoop responses");
    }

    ResponseStatus(Type type, chi::FinalState state) : m_type{type}
    {
        AMPERE_ASSERT(type == Type::Comp || type == Type::WriteData || type == Type::CompAck,
                      "ResponseStatus constructor with state is intended for Comp, WriteData, and CompAck types only");

        switch (state) {
        case chi::FinalState::I:
            m_encoding = 0b000;
            break;
        case chi::FinalState::SC:
            m_encoding = 0b001;
            break;
        case chi::FinalState::UC:
            m_encoding = 0b010;
            break;
        case chi::FinalState::UD:
            m_encoding = 0b110;
            break;
        case chi::FinalState::SD:
            m_encoding = 0b111;
            break;
        }
    }

    // TODO: Implement CompWrNoSnpDef and TagMatch constructors, which interpret the encoding
    // completely differently.

    friend bool operator==(const ResponseStatus& a, const ResponseStatus& b)
    {
        return a.type() == b.type() && a.encoding() == b.encoding();
    }

    friend std::ostream& operator<<(std::ostream& os, const ResponseStatus& resp)
    {
        if (resp.type() == Type::Comp || resp.type() == Type::WriteData || resp.type() == Type::CompAck) {
            switch (resp.encoding()) {
            case 0b000:
                os << "I";
                break;
            case 0b001:
                os << "SC";
                break;
            case 0b010:
                os << "UC";
                break;
            case 0b110:
                os << "UD_PD";
                break;
            case 0b111:
                os << "SD_PD";
                break;
            default:
                os << "Invalid";
                break;
            }
        } else if (resp.type() == Type::Snoop) {
            switch (resp.encoding()) {
            case 0b000:
                os << "I";
                break;
            case 0b001:
                os << "SC";
                break;
            case 0b010:
                os << "UC,UD";
                break;
            case 0b011:
                os << "SD";
                break;
            case 0b100:
                os << "I_PD";
                break;
            case 0b101:
                os << "SC_PD";
                break;
            case 0b110:
                os << "UC_PD";
                break;
            default:
                os << "Invalid";
                break;
            }
        } else {
            os << "Invalid";
        }

        return os;
    }

    Type type() const { return m_type; }
    uint8_t encoding() const { return m_encoding; }

    FinalState state() const
    {
        auto state_value = maybe_state();

        if (state_value)
            return *state_value;
        else
            throw InvalidResponseState{"InvalidResponseStatus queried for state"};
    }

    std::optional<FinalState> maybe_state() const
    {
        if (m_type == Type::None)
            return {};

        if (m_type == Type::WriteData || m_type == Type::Comp) {
            switch (m_encoding) {
            case 0b000:
                return FinalState::I;
            case 0b001:
                return FinalState::SC;
            case 0b010:
                return FinalState::UC;
            case 0b110:
                return FinalState::UD;
            case 0b111:
                return FinalState::SD;
            default:
                AMPERE_FATAL("invalid encoding");
            }
        } else if (m_type == Type::Snoop) {
            switch (m_encoding) {
            case 0b000:
                return FinalState::I;
            case 0b001:
                return FinalState::SC;
            case 0b010:
                return FinalState::UC; // or UD??
            case 0b011:
                return FinalState::SD;
            case 0b100:
                return FinalState::I; // +PD
            case 0b101:
                return FinalState::SC;
            case 0b110:
                return FinalState::UC;
            default:
                AMPERE_FATAL("invalid encoding");
            }
        } else {
            AMPERE_FATAL("not yet implemented");
        }

        return {};
    }

    FinalState state_or(FinalState defval) const
    {
        auto state_value = maybe_state();
        return state_value ? *state_value : defval;
    }

    bool pass_dirty() const
    {
        AMPERE_ASSERT(m_type != Type::None, "Invalid ResponseStatus queried for pass_dirty");

        if (m_type == Type::WriteData || m_type == Type::Comp || m_type == Type::Snoop) {
            return m_encoding & 0b100;
        } else {
            AMPERE_FATAL("not yet implemented");
            return false;
        }
    }

private:
    Type m_type;
    uint8_t m_encoding;
};

// Short-hands for combos when you know exactly what you want.
extern const ResponseStatus RespComp_I;
extern const ResponseStatus RespComp_SC;
extern const ResponseStatus RespComp_UC;
extern const ResponseStatus RespComp_UD_PD;
extern const ResponseStatus RespComp_SD_PD;

extern const ResponseStatus RespWriteData_I;
extern const ResponseStatus RespWriteData_SC;
extern const ResponseStatus RespWriteData_UC;
extern const ResponseStatus RespWriteData_UD_PD;
extern const ResponseStatus RespWriteData_SD_PD;

extern const ResponseStatus RespSnoop_I;
extern const ResponseStatus RespSnoop_SC;
extern const ResponseStatus RespSnoop_UC;
extern const ResponseStatus RespSnoop_UD;
extern const ResponseStatus RespSnoop_SD;
extern const ResponseStatus RespSnoop_I_PD;
extern const ResponseStatus RespSnoop_SC_PD;
extern const ResponseStatus RespSnoop_UC_PD;

struct Qos
{
public:
    enum Level : uint8_t {
        IRON = 0,
        GOLD = 8,
        PLATINUM = 12
    };

private:
    Level qos;

public:
    Qos() : qos{Level::IRON} {}
    Qos(uint8_t val) : qos{val} {}

    Level to_level()
    {
        if (qos >= PLATINUM)
            return Level::PLATINUM;
        else if (qos >= GOLD)
            return Level::GOLD;
        else
            return Level::IRON;
    }

    bool is_platinum() { return to_level() == Level::PLATINUM; }

    bool is_gold() { return to_level() == Level::GOLD; }

    bool is_iron() { return to_level() == Level::IRON; }
};

struct ReserveCustomerUseReq
{
private:
    uint32_t data;

public:
    ReserveCustomerUseReq() : data{0} {}

    explicit ReserveCustomerUseReq(uint32_t data) : data{data} {}

    ReserveCustomerUseReq(RsvdcReq value) { data = static_cast<int>(value); }

    bool operator==(const ReserveCustomerUseReq& other) const { return data == other.data; }

    bool CopyBackL2L3() const { return (data & static_cast<int>(RsvdcReq::CopyBackL2L3)); }

    bool EvictL3ToL2() const { return (data & static_cast<int>(RsvdcReq::EvictL3ToL2)); }

    bool P2PReq() const { return (data & static_cast<int>(RsvdcReq::P2PReq)); }

    bool LargeReq() const { return (data & static_cast<int>(RsvdcReq::LargeReq)); }

    int ReqLen() const { return (LargeReq() || P2PReq()) ? static_cast<int>(util::extract(data, 9, 7)) : 0; }

    int ReqLenBytes() const
    {
        switch (ReqLen()) {
        case 1:
            return 128;
        case 3:
            return 256;
        case 7:
            return 512;
        default:
            return 0;
        }
    }
};

struct ReserveCustomerUseData
{
private:
    uint32_t data;

public:
    ReserveCustomerUseData() : data{0} {}

    ReserveCustomerUseData(RsvdcDat value) { data = static_cast<int>(value); }

    bool operator==(const ReserveCustomerUseData& other) const { return data == other.data; }
};

class LocalRoutingId
{
public:
    LocalRoutingId() : LocalRoutingId(0, 0, 0, 0, 0) {}

    // This device-id only constructor is to facilitate testability.
    explicit LocalRoutingId(uint8_t device) : LocalRoutingId(0, 0, 0, 0, device) {}
    explicit LocalRoutingId(uint8_t port, uint8_t device) : LocalRoutingId(0, 0, 0, port, device) {}

    LocalRoutingId(uint8_t x, uint8_t y, uint8_t port, uint8_t device) : LocalRoutingId(x, y, 0, port, device) {}

    LocalRoutingId(uint8_t x, uint8_t y, uint8_t z, uint8_t port, uint8_t device)
        : m_x{x}, m_y{y}, m_z{z}, m_port{port}, m_device{device}
    {
    }

    LocalRoutingId(const LocalRoutingId& other) = default;
    // This equality operator does not compare all the fields by default because we want
    // to only compare the physical coordinates (X, Y, Z, NP) of LRID that represents a
    // unique agent id.
    bool operator==(const LocalRoutingId& other) const
    {
        return m_x == other.m_x && m_y == other.m_y && m_z == other.m_z && m_port == other.m_port
               && m_device == other.m_device;
    }

    // This comparison operator is arbitrary, but its existance allows LocalRoutingId to
    // be used as a key in, e.g., a std::map.
    bool operator<(const LocalRoutingId& other) const { return id() < other.id(); }

    uint8_t device() const { return m_device; }
    uint8_t port() const { return m_port; }
    uint8_t x() const { return m_x; }
    uint8_t y() const { return m_y; }
    uint8_t z() const { return m_z; }
    uint8_t t_z() const { return m_t_z; }
    bool xyoverride() const { return m_xyoverride; }

    void set_port(uint8_t port) { m_port = port; }
    void set_device(uint8_t device) { m_device = device; }
    void set_x(uint8_t x) { m_x = x; }
    void set_y(uint8_t y) { m_y = y; }
    void set_z(uint8_t z) { m_z = z; }
    void set_t_z(uint8_t t_z) { m_t_z = t_z; }
    void set_xyoverride(bool xyoverride) { m_xyoverride = xyoverride; }

    // Note that this is NOT structured like a real CHI LocalRoutingId, since we allow for
    // a larger number of bits per field.
    uint64_t id() const
    {
        return (static_cast<uint64_t>(m_z) << 32) | (m_x << 24) | (m_y << 16) | (m_port << 8) | m_device;
    }

    friend std::ostream& operator<<(std::ostream& os, const LocalRoutingId& lrid)
    {
        fmt::print(os,
                   "[{},{},{},{},{},{},{}]",
                   lrid.xyoverride(),
                   lrid.x(),
                   lrid.y(),
                   lrid.z(),
                   lrid.t_z(),
                   lrid.port(),
                   lrid.device());
        return os;
    }

    friend void from_json(const json& j, LocalRoutingId& lrid)
    {
        if (j.size() == 0) {
            lrid = LocalRoutingId{};
        } else {
            if (j.size() == 4) {
                uint8_t x, y, port, device;
                j.at(0).get_to(x);
                j.at(1).get_to(y);
                j.at(2).get_to(port);
                j.at(3).get_to(device);
                lrid = LocalRoutingId{x, y, port, device};
            } else if (j.size() == 5) {
                uint8_t x, y, z, port, device;
                j.at(0).get_to(x);
                j.at(1).get_to(y);
                j.at(2).get_to(z);
                j.at(3).get_to(port);
                j.at(4).get_to(device);
                lrid = LocalRoutingId{x, y, z, port, device};
            } else {
                throw std::length_error{"LocalRoutingId vector should have 4 or 5 elements"};
            }
        }
    }

    friend void to_json(json& j, const LocalRoutingId& lrid)
    {
        j = json{lrid.m_x, lrid.m_y, lrid.m_z, lrid.m_port, lrid.m_device};
    }

private:
    bool m_xyoverride = false;
    uint8_t m_x;
    uint8_t m_y;
    uint8_t m_z;
    uint8_t m_t_z = 0;
    uint8_t m_port;
    uint8_t m_device;
};

class NodeId
{
public:
    enum class DieType : uint8_t {
        CPU,
        IO
    };
    enum class NodeId_Type : uint8_t {
        Invalid,
        Local,
        Global
    };

    NodeId()
    {
        m_nodeid_type = NodeId_Type::Invalid;
        m_node_id.local_id.x = 0;
        m_node_id.local_id.y = 0;
        m_node_id.local_id.port = 0;
        m_node_id.local_id.device = 0;
    }

    NodeId(NodeId::NodeId_Type nid_type)
    {
        if (nid_type == NodeId_Type::Local || nid_type == NodeId_Type::Invalid) {
            m_nodeid_type = nid_type;
            m_node_id.local_id.x = 0;
            m_node_id.local_id.y = 0;
            m_node_id.local_id.port = 0;
            m_node_id.local_id.device = 0;
        } else if (nid_type == NodeId_Type::Global) {
            m_nodeid_type = NodeId_Type::Global;
            m_node_id.global_id.die_type = DieType::CPU;
            m_node_id.global_id.socket_id = 0;
            m_node_id.global_id.die_id = 0;
            m_node_id.global_id.agent_id = 0;
        }
    }

    // This device-id only constructor is to facilitate testability.
    explicit NodeId(uint8_t device)
    {
        m_nodeid_type = NodeId_Type::Local;
        m_node_id.local_id.x = 0;
        m_node_id.local_id.y = 0;
        m_node_id.local_id.port = 0;
        m_node_id.local_id.device = device;
    }

    NodeId(uint8_t x, uint8_t y, uint8_t port, uint8_t device)
    {
        m_nodeid_type = NodeId_Type::Local;
        m_node_id.local_id.x = x;
        m_node_id.local_id.y = y;
        m_node_id.local_id.port = port;
        m_node_id.local_id.device = device;
    }

    NodeId(DieType die_type, uint8_t socket_id, uint8_t die_id, uint16_t agent_id)
    {
        m_nodeid_type = NodeId_Type::Global;
        m_node_id.global_id.die_type = die_type;
        m_node_id.global_id.socket_id = socket_id;
        m_node_id.global_id.die_id = die_id;
        m_node_id.global_id.agent_id = agent_id;
    }

    NodeId(const NodeId& other) = default;

    bool is_valid() const { return m_nodeid_type != NodeId_Type::Invalid; }

    bool operator==(const NodeId& other) const
    {
        if (other.m_nodeid_type != m_nodeid_type)
            return false;

        if (m_nodeid_type == NodeId_Type::Invalid) {
            return true;
        } else if (m_nodeid_type == NodeId_Type::Local) {
            return m_node_id.local_id.x == other.m_node_id.local_id.x
                   && m_node_id.local_id.y == other.m_node_id.local_id.y
                   && m_node_id.local_id.port == other.m_node_id.local_id.port
                   && m_node_id.local_id.device == other.m_node_id.local_id.device;
        } else {
            return m_node_id.global_id.die_type == other.m_node_id.global_id.die_type
                   && m_node_id.global_id.die_id == other.m_node_id.global_id.die_id
                   && m_node_id.global_id.socket_id == other.m_node_id.global_id.socket_id
                   && m_node_id.global_id.agent_id == other.m_node_id.global_id.agent_id;
        }
    }

    bool operator!=(const NodeId& other) const { return !(*this == other); }

    // This comparison operator is arbitrary, but its existance allows NodeId to
    // be used as a key in, e.g., a std::map.
    bool operator<(const NodeId& other) const { return id() < other.id(); }

    void set_socket_id(uint8_t socket_id)
    {
        AMPERE_ASSERT(m_nodeid_type == NodeId_Type::Global, "cannot set socket_id on a non-global NodeId");
        m_node_id.global_id.socket_id = socket_id;
    }
    void set_die_id(uint8_t die_id)
    {
        AMPERE_ASSERT(m_nodeid_type == NodeId_Type::Global, "cannot set die_id on a non-global NodeId");
        m_node_id.global_id.die_id = die_id;
    }

    NodeId_Type nodeid_type() const { return m_nodeid_type; }

    uint8_t device() const
    {
        AMPERE_ASSERT(m_nodeid_type == NodeId_Type::Local, "device() should be used with local_id");
        return m_node_id.local_id.device;
    }
    uint8_t port() const
    {
        AMPERE_ASSERT(m_nodeid_type == NodeId_Type::Local, "port() should be used with local_id");
        return m_node_id.local_id.port;
    }
    uint8_t x() const
    {
        AMPERE_ASSERT(m_nodeid_type == NodeId_Type::Local, "x() should be used with local_id");
        return m_node_id.local_id.x;
    }
    uint8_t y() const
    {
        AMPERE_ASSERT(m_nodeid_type == NodeId_Type::Local, "y() should be used with local_id");
        return m_node_id.local_id.y;
    }

    DieType die_type() const
    {
        AMPERE_ASSERT(m_nodeid_type == NodeId_Type::Global, "die_type() should be used with global_id");
        return m_node_id.global_id.die_type;
    }
    uint8_t socket_id() const
    {
        AMPERE_ASSERT(m_nodeid_type == NodeId_Type::Global, "socket_id() should be used with global_id");
        return m_node_id.global_id.socket_id;
    }
    uint8_t die_id() const
    {
        AMPERE_ASSERT(m_nodeid_type == NodeId_Type::Global, "die_id() should be used with global_id");
        return m_node_id.global_id.die_id;
    }
    uint16_t agent_id() const
    {
        AMPERE_ASSERT(m_nodeid_type == NodeId_Type::Global, "agent_id() should be used with global_id");
        return m_node_id.global_id.agent_id;
    }
    uint8_t get_x() const
    {
        AMPERE_ASSERT(m_nodeid_type == NodeId_Type::Global, "get_x() should be used with global_id");
        return (m_node_id.global_id.agent_id & 0x30) >> 4;
    }
    uint8_t get_y() const
    {
        AMPERE_ASSERT(m_nodeid_type == NodeId_Type::Global, "get_y() should be used with global_id");
        return (m_node_id.global_id.agent_id & 0x380) >> 7;
    }

    // Note that this is NOT structured like a real CHI node id, since we allow for
    // a larger number of bits per field.
    uint64_t id() const
    {
        if (m_nodeid_type == NodeId_Type::Local) {
            return (static_cast<uint64_t>(m_node_id.local_id.x) << 24)
                   | (static_cast<uint64_t>(m_node_id.local_id.y) << 16)
                   | (static_cast<uint64_t>(m_node_id.local_id.port) << 8)
                   | static_cast<uint64_t>(m_node_id.local_id.device);
        } else {
            return (static_cast<uint64_t>((m_node_id.global_id.die_type == DieType::CPU) ? 0 : 1) << 32)
                   | (static_cast<uint64_t>(m_node_id.global_id.socket_id) << 24)
                   | (static_cast<uint64_t>(m_node_id.global_id.die_id) << 16)
                   | static_cast<uint64_t>(m_node_id.global_id.agent_id);
        }
    }

    friend std::ostream& operator<<(std::ostream& os, DieType nt) { return os << static_cast<int>(nt); }

    friend std::ostream& operator<<(std::ostream& os, const NodeId& node_id)
    {
        if (node_id.m_nodeid_type == NodeId_Type::Local)
            fmt::print(os, "[{},{},{},{}]", node_id.x(), node_id.y(), node_id.port(), node_id.device());
        else if (node_id.m_nodeid_type == NodeId_Type::Global)
            fmt::print(os, "{}.{}.{}", node_id.socket_id(), node_id.die_id(), node_id.agent_id());
        else
            fmt::print(os, "INV");

        return os;
    }

    friend void from_json(
        const json& j, NodeId& node_id, uint8_t default_socket_id, DieType default_die_type, uint8_t default_die_id)
    {
        if (j.is_object() == false) {
            if (j.size() != 4)
                throw std::length_error{"local NodeId vector should have exactly 4 elements"};

            uint8_t x, y, port, device;
            j.at(0).get_to(x);
            j.at(1).get_to(y);
            j.at(2).get_to(port);
            j.at(3).get_to(device);
            node_id = NodeId{x, y, port, device};
        } else {
            DieType die_type = default_die_type;
            uint8_t socket_id = default_socket_id;
            uint8_t die_id = default_die_id;
            uint16_t agent_id;
            if (j.contains("die_type"))
                die_type = j["die_type"];
            if (j.contains("socket_id"))
                socket_id = j["socket_id"];
            if (j.contains("die_id"))
                die_id = j["die_id"];
            agent_id = j["agent_id"];
            node_id = NodeId{die_type, socket_id, die_id, agent_id};
        }
    }

    friend void from_json(const json& j, NodeId& node_id) { from_json(j, node_id, 0, DieType::CPU, 0); }

    friend void to_json(json& j, const NodeId& node_id)
    {
        if (node_id.m_nodeid_type == NodeId_Type::Local)
            j = json{node_id.m_node_id.local_id.x,
                     node_id.m_node_id.local_id.y,
                     node_id.m_node_id.local_id.port,
                     node_id.m_node_id.local_id.device};
        else
            j = json{{"die_type", node_id.m_node_id.global_id.die_type},
                     {"socket_id", node_id.m_node_id.global_id.socket_id},
                     {"die_id", node_id.m_node_id.global_id.die_id},
                     {"agent_id", node_id.m_node_id.global_id.agent_id}};
    }

#ifdef USE_BOOST_SERIALIZATION
    template<typename Archive>
    void save(Archive& ar, const unsigned int version) const
    {
        if (m_nodeid_type == NodeId_Type::Invalid) {
            ar << 0;
        } else if (m_nodeid_type == NodeId_Type::Global) {
            ar << 1;
            ar << m_node_id.global_id.die_type;
            ar << m_node_id.global_id.socket_id;
            ar << m_node_id.global_id.die_id;
            ar << m_node_id.global_id.agent_id;
        } else {
            ar << 2;
            ar << m_node_id.local_id.x;
            ar << m_node_id.local_id.y;
            ar << m_node_id.local_id.port;
            ar << m_node_id.local_id.device;
        }
    }

    template<typename Archive>
    void load(Archive& ar, const unsigned int version)
    {
        int type;

        ar >> type;

        if (type == 1) {
            m_nodeid_type = NodeId_Type::Global;
            ar >> m_node_id.global_id.die_type;
            ar >> m_node_id.global_id.socket_id;
            ar >> m_node_id.global_id.die_id;
            ar >> m_node_id.global_id.agent_id;
        } else if (type == 2) {
            m_nodeid_type = NodeId_Type::Local;
            ar >> m_node_id.local_id.x;
            ar >> m_node_id.local_id.y;
            ar >> m_node_id.local_id.port;
            ar >> m_node_id.local_id.device;
        }
    }
    BOOST_SERIALIZATION_SPLIT_MEMBER()
#endif // USE_BOOST_SERIALIZATION

private:
    NodeId_Type m_nodeid_type{NodeId_Type::Local};
    union node_id
    {
        struct
        {
            uint8_t x;
            uint8_t y;
            uint8_t port;
            uint8_t device;
        } local_id;
        struct
        {
            DieType die_type;
            uint8_t socket_id;
            uint8_t die_id;
            uint16_t agent_id;
        } global_id;
    };
    node_id m_node_id;
};

struct Request : public sc::Transaction
{
    Request(NodeId src_id, NodeId tgt_id, uint16_t txn, RequestOpcode opcode, archx::PhysAddr addr);

    Qos QoS;              /* QoS Priority Value, range 0-15 */
    NodeId TgtID;         /* Target Identifier associated with this message */
    NodeId SrcID;         /* Source Identifier associated with this message */
    LocalRoutingId LRID;  /* Local Routing ID, for compatibility with Matrix */
    NodeId LDID;          /* for compatibility with Rogue */
    uint16_t TxnID;       /* Transaction Identifier associated with this message.
                             Multiple outstanding transactions from a single source node must have a unique ID. */
    uint8_t LPID;         /* Logical Processor Identifier */
    NodeId ReturnNID;     /* Return NID identifies the node to which the SN-F sends the response */
    uint16_t ReturnTxnID; /* Return TxnID Identifies the value the SN-F must use in the TxnID field of a response. */
    NodeId StashNID;      /* Identifies the target of the stash request */
    bool StashNIDValid;   /* Use Stash NID value */
    uint16_t StashLPID;   /* Stash Logical Processor ID */
    bool StashLPIDValid;  /* Use StashLPID value */
    RequestOpcode Opcode; /* Operation to be carried out */
    archx::PhysAddr Addr; /* Address associated with this message */
    bool NS;              /* Indicates a secure access */
    uint8_t Size;         /* Size of data associated with the transaction, Valid, 1,2,4,8,16,32,64 */
    bool AllowRetry;      /* Allow Retries, RetryAck response permitted */
    uint8_t PCrdType;     /* Indicates the protocol credit type being granted or returned, 0 to 15 */
    bool ExpCompAck;      /* Transaction includes a CompAck response */
    uint8_t MemAttr;      /* Memory attributes associated with the transaction */
    bool SnpAttr;         /* Is Snoopable */
    bool DoDWT;           /* Do direct write transfer */
    bool SnoopMe;         /* Indicates the Home must determine whether to send a snoop to the Requester */
    bool LikelyShared;    /* Likely to be shared by another RN */
    bool Excl;            /* 0 = Normal transaction, 1 = Exclusive transaction */
    RequestOrder Order;   /* Ordering requirements for transaction */
    bool Endian;          /* 0 = Little, 1 = Big */
    bool TraceTag;        /* Tag this packet for tracing purposes */
    ReserveCustomerUseReq RSVDC; /* Optional, Reserved for customer use Valid = 0, 4, 8 12, 16, 24 and 32 bits */
};

struct Snoop : public sc::Transaction
{
    Snoop(NodeId src_id, uint16_t txn, SnoopOpcode opcode, archx::PhysAddr addr);

    Qos QoS;              /* QoS Priority Value, range 0-15 */
    uint16_t TxnID;       /* Transaction Identifier associated with this message.
                             Multiple outstanding transactions from a single source node must have a unique ID. */
    NodeId FwdNID;        /* Identifies the requester to which a response can be forwarded */
    uint16_t FwdTxnID;    /* Identifies the TxnID of the original request associated with the this message */
    uint16_t StashLPID;   /* Stash Logical Processor ID */
    bool StashLPIDValid;  /* Use StashLPID value */
    uint16_t VMIDExt;     /* Virtual Machine Identifier extension */
    NodeId SrcID;         /* Source Identifier associated with this message */
    LocalRoutingId LRID;  /* Local Routing ID, for compatibility with Matrix */
    SnoopOpcode Opcode;   /* Operation to be carried out */
    archx::PhysAddr Addr; /* Address associated with this message */
    bool NS;              /* Indicates a secure access */
    bool DoNotGoToSD;     /* Do not transition to the SD state */
    bool DoNotDataPull;   /* Do not data pull */
    bool RetToSrc;        /* Return to source, Request Snoopee to return a copy of the cacheline to home */
    bool TraceTag;        /* Tag this packet for tracing purposes */
    bool InvL3Copy;       /* Ampere: Hint to core invalidate L3 copy if set */
};

struct Data : public sc::Transaction
{
    Data(NodeId src_id, NodeId tgt_id, uint16_t txn, DataOpcode opcode);

    Qos QoS;                 /* QoS Priority Value, range 0-15 */
    NodeId TgtID;            /* Target Identifier associated with this message */
    NodeId SrcID;            /* Source Identifier associated with this message */
    LocalRoutingId LRID;     /* Local Routing ID, for compatibility with Matrix */
    uint16_t TxnID;          /* Transaction Identifier associated with this message.
                                Multiple outstanding transactions from a single source node must have a unique ID. */
    NodeId HomeNID;          /* Home identifier associated with the original request */
    uint16_t DBID;           /* Data buffer identifier */
    DataOpcode Opcode;       /* Operation to be carried out */
    ResponseErr RespErr;     /* Response Error indicates error status of response */
    ResponseStatus Resp;     /* Response Status */
    ResponseStatus FwdState; /* Forward State indicates the state in the CompData sent from the Snoopee to Requester */
    DataPull DatPull;        /* Data Pull indicates the inclusion of a read request aka data pull */
    DataSource DatSource;    /* Identifies the sender of the data response */
    CBusyState
        CBusyS;   /* Completer Busy.  A mechanism for complter of transaction to indicate its activity level. 3 bits */
    uint8_t CCID; /* Critical Chunk Identifier, 2 bit field, use accessors */
    uint8_t DataID;               /* Data Identifier, Indicates the relative position of the data chunk within
                                     the 512 bit cache line being transferred */
    uint64_t BE;                  /* Byte Enabled, indicates if the byte of data corresponding to this byte enabled
                                     bit is valid */
    void* DataPtr;                /* Actual data of size 128, 256, 512 width */
    bool DataCheck;               /* Parity bit that generates Odd byte parity */
    bool Poison;                  /* Indicates if the data has an error and must not be consumed */
    bool TraceTag;                /* Tag this packet for tracing purposes */
    ReserveCustomerUseData RSVDC; /* Optional, Reserved for customer use Valid = 0, 4, 8 12, 16, 24 and 32 bits */
};

struct Response : public sc::Transaction
{
    Response(NodeId src_id, NodeId tgt_id, uint16_t txn, ResponseOpcode opcode);

    Qos QoS;             /* QoS Priority Value, range 0-15 */
    NodeId TgtID;        /* Target Identifier associated with this message */
    NodeId SrcID;        /* Source Identifier associated with this message */
    LocalRoutingId LRID; /* Local Routing ID, for compatibility with Matrix */
    uint16_t TxnID;      /* Transaction Identifier associated with this message.
                            Multiple outstanding transactions from a single source node must have a unique ID. */
    CBusyState
        CBusyS;    /* Completer Busy.  A mechanism for complter of transaction to indicate its activity level. 3 bits */
    uint16_t DBID; /* Data buffer identifier */
    uint8_t PCrdType;        /* Indicates the protocol credit type being granted or returned, 0 to 15 */
    ResponseOpcode Opcode;   /* Operation to be carried out */
    ResponseErr RespErr;     /* Response Error indicates error status of response */
    ResponseStatus Resp;     /* Response Status */
    ResponseStatus FwdState; /* Forward State indicates the state in the CompData sent from the Snoopee to Requester */
    DataPull DatPull;        /* Data Pull indicates the inclusion of a read request aka data pull */
    bool TraceTag;           /* Tag this packet for tracing purposes */
};

} // namespace chi
} // namespace ampere::mesh

namespace ampere::sc {

// Should not be here, but removing it requires a bunch of other changes that I don't
// want in this commit.
using namespace ampere::mesh::chi;

} // namespace ampere::sc

template<>
struct fmt::formatter<ampere::mesh::chi::ResponseStatus> : fmt::ostream_formatter
{};
template<>
struct fmt::formatter<ampere::mesh::chi::LocalRoutingId> : fmt::ostream_formatter
{};
template<>
struct fmt::formatter<ampere::mesh::chi::NodeId::DieType> : fmt::ostream_formatter
{};
template<>
struct fmt::formatter<ampere::mesh::chi::NodeId> : fmt::ostream_formatter
{};

#endif // AMPERE_MESH_CHI_TYPES_H

/*
 * vim: sw=4:ts=4:et:si:cindent:cino=\:0,(0,g0,t0:
 */
