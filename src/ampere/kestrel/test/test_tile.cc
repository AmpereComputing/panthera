//
// Copyright (c) 2024 Ampere Computing. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause
//
#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "ampere/archx/instruction.h"
#include "ampere/kestrel/mesh/tile.h"
#include "ampere/sc/testing/test_module.h"
#include "ampere/mesh/test/mock_addr_map.h"
#include "ampere/mesh/test/mock_mesh_port.h"

namespace ampere::kestrel {

class TileTest : public sc::TestModule, public MeshConfigIfc
{
public:
    TileTest()
        : sc::TestModule({{"tile.devices", R"(
            [
                { "type": "MeshInjector", "position": [0, 0], "prefix": "core", "agent_id": 0 },
                { "type": "MeshInjector", "position": [0, 1], "prefix": "core", "agent_id": 1 },
                { "type": "MeshInjector", "position": [0, 2], "prefix": "core", "agent_id": 2 },
                { "type": "MeshInjector", "position": [0, 3], "prefix": "core", "agent_id": 3 },
                { "type": "MeshInjector", "position": [1, 0], "prefix": "aice", "agent_id": 4 }
            ]
            )"_json},
                          {"~tile.cpx_0.dev_[0-3].req_in.size", 4},
                          {"~tile.cpx_0.dev_[0-3].rsp_in.size", 4},
                          {"~tile.cpx_0.dev_[0-3].snp_in.size", 4},
                          {"~tile.cpx_0.dev_[0-3].dat_in.size", 4},
                          {"tile.rtr.req_p0.size", 4},
                          {"tile.rtr.rsp_p0.size", 4},
                          {"tile.rtr.snp_p0.size", 4},
                          {"tile.rtr.dat_p0.size", 4},
                          {"tile.rtr.req_p1.size", 4},
                          {"tile.rtr.rsp_p1.size", 4},
                          {"tile.rtr.snp_p1.size", 4},
                          {"tile.rtr.dat_p1.size", 4},
                          {"~tile.core_[0-3].source.type", "UopSourceProg"},
                          {"~tile.core_[0-3].source.decoders", R"(["furiosa"])"_json},
                          {"~tile.core_[0-3].sam.type", "CDRN_SAM"},
                          {"~tile.core_[0-3].sam.system_address_map.connect", "system_address_map"},
                          {"~tile.core_[0-3].sam.tgt_lut", "[5]"_json},
                          {"tile.aice_0.source.type", "UopSourceProg"},
                          {"tile.aice_0.source.decoders", R"(["furiosa"])"_json},
                          {"tile.aice_0.sam.type", "CDRN_SAM"},
                          {"tile.aice_0.sam.system_address_map.connect", "system_address_map"},
                          {"tile.aice_0.sam.tgt_lut", "[5]"_json},
                          {"tile.rtr.local_lut",
                           std::map<uint8_t, LocalRoutingId>{{0, LocalRoutingId{0, 0, 0, 0}},
                                                             {1, LocalRoutingId{0, 0, 0, 1}},
                                                             {2, LocalRoutingId{0, 0, 0, 2}},
                                                             {3, LocalRoutingId{0, 0, 0, 3}},
                                                             {4, LocalRoutingId{0, 0, 1, 0}},
                                                             {5, LocalRoutingId{0, 1, 0, 0}}}}})
    {
    }

    int data_bus_width() const override { return 512; }
    int channel(const chi::NodeId id) const override { return 0; }

    void setup_program(const std::string& dev, json program)
    {
        get_environment()->parameters.add_setting(dev, program, "test bench");
    }

    void build()
    {
        tile = std::make_unique<Tile>("tile");
        auto& tile_port = tile->get_mesh_port();
        tile_port.rx().req[0].bind(mock_mesh_port.tx.req);
        tile_port.rx().rsp[0].bind(mock_mesh_port.tx.rsp);
        tile_port.rx().snp[0].bind(mock_mesh_port.tx.snp);
        tile_port.rx().dat[0].bind(mock_mesh_port.tx.dat);

        tile_port.tx().req[0].bind(mock_mesh_port.rx.req);
        tile_port.tx().rsp[0].bind(mock_mesh_port.rx.rsp);
        tile_port.tx().snp[0].bind(mock_mesh_port.rx.snp);
        tile_port.tx().dat[0].bind(mock_mesh_port.rx.dat);

        tile->config.bind(*this);
    }

protected:
    std::unique_ptr<Tile> tile;
    MockMeshPort mock_mesh_port;
    MockSystemAddressMap system_address_map{"system_address_map"};
};

TEST_F(TileTest, CanCreateTile)
{
    setup_program("tile.core_0.source.program", R"({
"code": [
    {"op": "LDR_64_ldst_pos", "fields": {"imm12": 0, "Rn": 1, "Rt": 2}, "pa": 8192}
]}
)"_json);

    setup_program("tile.core_1.source.program", R"({
"code": [
    {"op": "LDR_64_ldst_pos", "fields": {"imm12": 0, "Rn": 1, "Rt": 2}, "pa": 8256}
]}
)"_json);

    setup_program("tile.core_2.source.program", R"({
"code": [
    {"op": "LDR_64_ldst_pos", "fields": {"imm12": 0, "Rn": 1, "Rt": 2}, "pa": 8320}
]}
)"_json);

    setup_program("tile.core_3.source.program", R"({
"code": [
    {"op": "LDR_64_ldst_pos", "fields": {"imm12": 0, "Rn": 1, "Rt": 2}, "pa": 8384}
]}
)"_json);

    setup_program("tile.aice_0.source.program", R"({
"code": [
    {"op": "LDR_64_ldst_pos", "fields": {"imm12": 0, "Rn": 1, "Rt": 2}, "pa": 8448}
]}
)"_json);

    build();

    EXPECT_CALL(mock_mesh_port.rx.req, can_put)
        .WillOnce(Return(true))
        .WillOnce(Return(true))
        .WillOnce(Return(true))
        .WillOnce(Return(true))
        .WillOnce(Return(true));

    EXPECT_CALL(mock_mesh_port.rx.req, put(HasAddress(8192_pa)));
    EXPECT_CALL(mock_mesh_port.rx.req, put(HasAddress(8256_pa)));
    EXPECT_CALL(mock_mesh_port.rx.req, put(HasAddress(8320_pa)));
    EXPECT_CALL(mock_mesh_port.rx.req, put(HasAddress(8384_pa)));
    EXPECT_CALL(mock_mesh_port.rx.req, put(HasAddress(8448_pa)));

    tick(10);
}

} // namespace ampere::kestrel
