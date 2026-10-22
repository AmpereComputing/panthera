/*
 * Copyright (c) 2024 Ampere Computing. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 */

#pragma once

#include "gtest/gtest.h"

#include "model.h"
#include "types.h"

namespace ampere::lynx {
class LinkGraph;
}

using namespace ampere::lynx;

void load_model(const std::string& fname, Model& model);
void load_rules(const std::string& fname, Rules& rules, const Model& model);
void load_graph(const std::string& fname, LinkGraph& rules, const Model& model);

extern void setup_mesh1x1(Model& model, u8 die_id = 0);
extern void setup_mesh2x1(Model& model, u8 die_id = 0);
extern void setup_mesh2x2(Model& model, u8 die_id = 0);
extern void setup_domino_half_quad(Model& model);
extern void setup_domino_4iod(Model& model);
extern void setup_domino(Model& model);
