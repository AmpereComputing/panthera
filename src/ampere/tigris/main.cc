//
// Copyright (c) 2024-2025 Ampere Computing. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause
//

#include "ampere/tigris/tigris.h"
#include "ampere/tools/is_tools/is_base.h"

int sc_main(int argc, char** argv)
{
    return InstructionStreamRunner<ampere::tigris::Tigris>::init_and_run(argc, argv);
}
