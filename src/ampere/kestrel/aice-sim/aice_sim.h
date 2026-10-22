//
// Copyright (c) 2018-2025 Ampere Computing. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause
//

#pragma once
#include <iostream>
#include <memory>
#include <bitset>
#include <vector>
#include <deque>
#include <algorithm>
#include <map>
#include <unordered_map>
#include <unordered_set>
#include <random>
#include <cmath>
#include <format>
#include <iostream>

#include "ampere/event_stream/event_stream_proto.h"
#include "ampere/sc/connect.h"
#include "ampere/sc/credit.h"
#include "ampere/sc/event.h"
#include "ampere/sc/module.h"
#include "ampere/sc/transaction.h"

#include "ampere/kestrel/aice-sim/aice_instruction.h"
#include "ampere/kestrel/aice-sim/aice_test_instr.h"
#include "ampere/protobuf_utils/protobuf_stream.h"
#include "ampere/kestrel/aice-sim/aice_stream.pb.h"
#include "ampere/kestrel/aice-sim/aice_instruction_stream.h"
#include "ampere/kestrel/aice-sim/aice_common.h"
#include "ampere/kestrel/aice-sim/aice_tmem_fsm.h"
#include "ampere/kestrel/aice-sim/aice_mem_req.h"
#include "ampere/kestrel/aice-sim/aice_tmem.h"
#include "ampere/kestrel/aice-sim/dtype.hh"
#include "ampere/kestrel/aice-sim/aice_ramp.h"

namespace ampere::kestrel::aice {

using namespace ampere;

static std::unordered_multimap<opcode_t, resource_t> required_resources{
    {opcode_t::UNDEFINED, resource_t::INVALID},

    // halt waits for everything
    {opcode_t::SYNCHALT, resource_t::TMEM_FSMS},
    {opcode_t::SYNCHALT, resource_t::LOADQ},
    {opcode_t::SYNCHALT, resource_t::STOREQ},
    {opcode_t::SYNCHALT, resource_t::MAR},
    {opcode_t::SYNCHALT, resource_t::MAR_BBUS},
    {opcode_t::SYNCHALT, resource_t::ALU},
    {opcode_t::SYNCHALT, resource_t::WB},

    {opcode_t::CSELGTZ, resource_t::ALU},
    {opcode_t::CSELZ, resource_t::ALU},
    {opcode_t::ADD, resource_t::ALU},
    {opcode_t::AND, resource_t::ALU},
    {opcode_t::SUB, resource_t::ALU},
    {opcode_t::MUL, resource_t::ALU},
    {opcode_t::LSHIFT, resource_t::ALU},
    {opcode_t::RASHIFT, resource_t::ALU},
    {opcode_t::ADDI, resource_t::ALU},
    {opcode_t::ANDI, resource_t::ALU},
    {opcode_t::MULI, resource_t::ALU},
    {opcode_t::LSHIFTI, resource_t::ALU},
    {opcode_t::RASHIFTI, resource_t::ALU},
    {opcode_t::BLT, resource_t::ALU},
    {opcode_t::BGE, resource_t::ALU},
    {opcode_t::BIR, resource_t::ALU},

    // all these WB are delayed by the latency of execution
    {opcode_t::CSELGTZ, resource_t::WB},
    {opcode_t::CSELZ, resource_t::WB},
    {opcode_t::ADD, resource_t::WB},
    {opcode_t::AND, resource_t::WB},
    {opcode_t::SUB, resource_t::WB},
    {opcode_t::MUL, resource_t::WB},
    {opcode_t::LSHIFT, resource_t::WB},
    {opcode_t::RASHIFT, resource_t::WB},
    {opcode_t::ADDI, resource_t::WB},
    {opcode_t::ANDI, resource_t::WB},
    {opcode_t::MULI, resource_t::WB},
    {opcode_t::LSHIFTI, resource_t::WB},
    {opcode_t::RASHIFTI, resource_t::WB},
    {opcode_t::LOADIMR, resource_t::LOADQ},
    {opcode_t::LOADMX, resource_t::LOADQ},
    {opcode_t::LOADMXU_FP8, resource_t::LOADQ},
    {opcode_t::LOADMXU_BF16, resource_t::LOADQ},
    {opcode_t::LOADSCALE_128, resource_t::LOADQ},
    {opcode_t::LOADSCALE_256, resource_t::LOADQ},
    {opcode_t::LOADSCALE_512, resource_t::LOADQ},
    {opcode_t::SYNCREADDONE, resource_t::LOADQ},
    {opcode_t::SYNCWAIT, resource_t::INVALID},
    {opcode_t::RESTARTABLE, resource_t::INVALID},
    {opcode_t::SYNCWRITEDONE, resource_t::STOREQ},
    {opcode_t::STOREIMR, resource_t::STOREQ},
    {opcode_t::STOREAMRV, resource_t::STOREQ},
    {opcode_t::STOREAMR64, resource_t::STOREQ},
    {opcode_t::STOREAMR32, resource_t::STOREQ},
    {opcode_t::STOREAMR16, resource_t::STOREQ},
    {opcode_t::STOREAMR8, resource_t::STOREQ},
    {opcode_t::MOVB, resource_t::MAR_BBUS},
    {opcode_t::MATMULA, resource_t::MAR},
};

class Fetch : public sc::Module
{
public:
    sc::Out<aice_instruction_t> out{"out"};
    sc::In<sc::Credit> decoder_credit_release_in{"decoder_credit_release_in"};

    explicit Fetch(const sc_core::sc_module_name& name) : sc::Module(name) {}

    bool run() override
    {
        while (decoder_credit_release_in->can_take()) {
            decoder_credit_release_in->take()->release();
        }
        while (decoder_credits.get_count() > 0 && out->can_put()) {
            auto txn = get_next_instruction();
            txn->decoder_credit = decoder_credits.acquire();
            emit_fetch_event(txn);
            DBG("Fetch {} {:#x} {}", txn->inum, txn->pc, txn->disasm());
            out->put(txn);
        }
        return true;
    } // run

private:
    sc::Param<std::string> input_trace{"input_trace", "Input protobuf trace", ""};
    bool done{false};
    std::unique_ptr<is::InstructionStreamReader> trc_reader{nullptr};

    sc::Event fetch_event{"fetch", "Fetch instruction"};
    sc::EventData<std::string> evd_fetch_disasm{"disasm", "Disassembly"};
    sc::EventData<unsigned> evd_inum{"inum", "Instruction number"};
    sc::CreditPool decoder_credits{"decoder"};

    static unsigned opcode_to_num_accumulators(opcode_t opcode)
    {
        switch (opcode) {
        case opcode_t::STOREAMR64:
            return 64;
        case opcode_t::STOREAMR32:
            return 32;
        case opcode_t::STOREAMR16:
            return 16;
        case opcode_t::STOREAMR8:
            return 8;
        default:
            THROW("Throw unhandled store opcode {}", util::enum_to_string<opcode_t>(opcode));
        }
    }

    aice_instruction_t make_aice_instr(const is::Instruction& instruction)
    {
        // convert a protobuf trace instruction in to a simulator instruction
        std::uint64_t pc = instruction.program_counter().pa();
        opcode_t opcode = static_cast<opcode_t>(instruction.opcode());
        switch (opcode) {
        case opcode_t::SYNCHALT:
            return make<aice_instruction_t>(make_synchalt(pc));
        case opcode_t::DEBUG:
            return make<aice_instruction_t>(make_debug(pc));
        case opcode_t::SYNCWAIT:
            return make<aice_instruction_t>(make_syncwait(pc));
        case opcode_t::RESTARTABLE:
            return make<aice_instruction_t>(make_restartable(pc));
        case opcode_t::SYNCREADDONE:
            return make<aice_instruction_t>(make_syncreaddone(pc));
        case opcode_t::SYNCWRITEDONE:
            return make<aice_instruction_t>(make_syncwritedone(pc));
        case opcode_t::CSELGTZ: {
            regidx_t gdst = instruction.gpr_dst(0).number();
            regidx_t gsrc1 = instruction.gpr_src(0).number();
            regidx_t gsrc2 = instruction.gpr_src(1).number();
            regidx_t gsrc3 = instruction.gpr_src(2).number();
            return make<aice_instruction_t>(make_cseltgz(pc, gdst, gsrc1, gsrc2, gsrc3));
        }
        case opcode_t::CSELZ: {
            regidx_t gdst = instruction.gpr_dst(0).number();
            regidx_t gsrc1 = instruction.gpr_src(0).number();
            regidx_t gsrc2 = instruction.gpr_src(1).number();
            regidx_t gsrc3 = instruction.gpr_src(2).number();
            return make<aice_instruction_t>(make_cselz(pc, gdst, gsrc1, gsrc2, gsrc3));
        }
        case opcode_t::ADD: {
            regidx_t gdst = instruction.gpr_dst(0).number();
            regidx_t gsrc1 = instruction.gpr_src(0).number();
            regidx_t gsrc2 = instruction.gpr_src(1).number();
            return make<aice_instruction_t>(make_add(pc, gdst, gsrc1, gsrc2));
        }
        case opcode_t::ADDI: {
            regidx_t gdst = instruction.gpr_dst(0).number();
            regidx_t gsrc1 = instruction.gpr_src(0).number();
            std::int64_t imm = instruction.imm_src(0);
            return make<aice_instruction_t>(make_addi(pc, gdst, gsrc1, imm));
        }
        case opcode_t::AND: {
            regidx_t gdst = instruction.gpr_dst(0).number();
            regidx_t gsrc1 = instruction.gpr_src(0).number();
            regidx_t gsrc2 = instruction.gpr_src(1).number();
            return make<aice_instruction_t>(make_and(pc, gdst, gsrc1, gsrc2));
        }
        case opcode_t::ANDI: {
            regidx_t gdst = instruction.gpr_dst(0).number();
            regidx_t gsrc1 = instruction.gpr_src(0).number();
            std::int64_t imm = instruction.imm_src(0);
            return make<aice_instruction_t>(make_andi(pc, gdst, gsrc1, imm));
        }
        case opcode_t::SUB: {
            regidx_t gdst = instruction.gpr_dst(0).number();
            regidx_t gsrc1 = instruction.gpr_src(0).number();
            regidx_t gsrc2 = instruction.gpr_src(1).number();
            return make<aice_instruction_t>(make_sub(pc, gdst, gsrc1, gsrc2));
        }
        case opcode_t::MUL: {
            regidx_t gdst = instruction.gpr_dst(0).number();
            regidx_t gsrc1 = instruction.gpr_src(0).number();
            regidx_t gsrc2 = instruction.gpr_src(1).number();
            return make<aice_instruction_t>(make_mul(pc, gdst, gsrc1, gsrc2));
        }
        case opcode_t::LSHIFT: {
            regidx_t gdst = instruction.gpr_dst(0).number();
            regidx_t gsrc1 = instruction.gpr_src(0).number();
            regidx_t gsrc2 = instruction.gpr_src(1).number();
            return make<aice_instruction_t>(make_lshift(pc, gdst, gsrc1, gsrc2));
        }
        case opcode_t::RASHIFT: {
            regidx_t gdst = instruction.gpr_dst(0).number();
            regidx_t gsrc1 = instruction.gpr_src(0).number();
            regidx_t gsrc2 = instruction.gpr_src(1).number();
            return make<aice_instruction_t>(make_rashift(pc, gdst, gsrc1, gsrc2));
        }
        case opcode_t::MULI: {
            regidx_t gdst = instruction.gpr_dst(0).number();
            regidx_t gsrc1 = instruction.gpr_src(0).number();
            std::int64_t imm = instruction.imm_src(0);
            return make<aice_instruction_t>(make_muli(pc, gdst, gsrc1, imm));
        }
        case opcode_t::LSHIFTI: {
            regidx_t gdst = instruction.gpr_dst(0).number();
            regidx_t gsrc1 = instruction.gpr_src(0).number();
            std::int64_t imm = instruction.imm_src(0);
            return make<aice_instruction_t>(make_lshifti(pc, gdst, gsrc1, imm));
        }
        case opcode_t::RASHIFTI: {
            regidx_t gdst = instruction.gpr_dst(0).number();
            regidx_t gsrc1 = instruction.gpr_src(0).number();
            std::int64_t imm = instruction.imm_src(0);
            return make<aice_instruction_t>(make_rashifti(pc, gdst, gsrc1, imm));
        }
        case opcode_t::BLT: {
            regidx_t gsrc1 = instruction.gpr_src(0).number();
            regidx_t gsrc2 = instruction.gpr_src(1).number();
            std::int64_t imm = instruction.imm_src(0);
            return make<aice_instruction_t>(make_blt(pc, gsrc1, gsrc2, imm));
        }
        case opcode_t::BGE: {
            regidx_t gsrc1 = instruction.gpr_src(0).number();
            regidx_t gsrc2 = instruction.gpr_src(1).number();
            std::int64_t imm = instruction.imm_src(0);
            return make<aice_instruction_t>(make_bge(pc, gsrc1, gsrc2, imm));
        }
        case opcode_t::BIR: {
            regidx_t gsrc1 = instruction.gpr_src(0).number();
            return make<aice_instruction_t>(make_bir(pc, gsrc1));
        }
        case opcode_t::STOREIMR: {
            regidx_t gpr_mem_idx = instruction.mem_dst(0).number(); // dst addr
            regidx_t src_mem_start = instruction.mem_dst(0).pa();   // dst addr in memory
            regidx_t gpr_imr = instruction.imr_src(0).number();     // imr dst offset gpr
            regidx_t imr_idx = instruction.imr_src(0).value();      // imr idx
            regidx_t gpr_stride = instruction.gpr_src(0).number();
            regidx_t stride = instruction.gpr_src(0).value();
            return make<aice_instruction_t>(
                make_storeimr(pc, gpr_mem_idx, src_mem_start, gpr_stride, stride, gpr_imr, imr_idx));
        }
        case opcode_t::STOREAMR64:
        case opcode_t::STOREAMR32:
        case opcode_t::STOREAMR16:
        case opcode_t::STOREAMR8: {
            regidx_t gpr_mem_idx = instruction.mem_dst(0).number(); // dst addr
            regidx_t src_mem_start = instruction.mem_dst(0).pa();   // dst addr in memory
            regidx_t gpr_amr = instruction.amr_src(0).number();     // amr dst offset gpr
            regidx_t amr_idx = instruction.amr_src(0).value();      // amr idx
            regidx_t gpr_stride = instruction.gpr_src(0).number();
            regidx_t stride = instruction.gpr_src(0).value();
            aice_emu::dtype_t dtype = static_cast<aice_emu::dtype_t>(instruction.dtype());
            const unsigned nacc = opcode_to_num_accumulators(opcode);

            return make<aice_instruction_t>(
                make_storeamr(pc, gpr_mem_idx, src_mem_start, gpr_stride, stride, gpr_amr, amr_idx, dtype, nacc));
        }
        case opcode_t::STOREAMRV: {
            regidx_t gpr_mem_idx = instruction.mem_dst(0).number(); // dst addr
            regidx_t src_mem_start = instruction.mem_dst(0).pa();   // dst addr in memory
            regidx_t gpr_amr = instruction.amr_src(0).number();     // amr dst offset gpr
            regidx_t amr_idx = instruction.amr_src(0).value();      // amr idx
            aice_emu::dtype_t dtype = static_cast<aice_emu::dtype_t>(instruction.dtype());

            return make<aice_instruction_t>(make_storeamrv(pc, gpr_mem_idx, src_mem_start, gpr_amr, amr_idx, dtype));
        }

        case opcode_t::LOADSCALE_128: {
            regidx_t gpr_imr = instruction.gpr_src(0).number();      // imr dst offset gpr
            regidx_t imr_idx = instruction.gpr_src(0).value();       // imr idx
            regidx_t gpr_mem_idx = instruction.gpr_src(1).number();  // src addr
            regidx_t src_mem_start = instruction.gpr_src(1).value(); // src addr in memory
            return make<aice_instruction_t>(make_loadscale_128(pc, gpr_imr, imr_idx, gpr_mem_idx, src_mem_start));
        }
        case opcode_t::LOADSCALE_256: {
            regidx_t gpr_imr = instruction.gpr_src(0).number();      // imr dst offset gpr
            regidx_t imr_idx = instruction.gpr_src(0).value();       // imr idx
            regidx_t gpr_mem_idx = instruction.gpr_src(1).number();  // src addr
            regidx_t src_mem_start = instruction.gpr_src(1).value(); // src addr in memory
            return make<aice_instruction_t>(make_loadscale_256(pc, gpr_imr, imr_idx, gpr_mem_idx, src_mem_start));
        }
        case opcode_t::LOADSCALE_512: {
            regidx_t gpr_imr = instruction.gpr_src(0).number();      // imr dst offset gpr
            regidx_t imr_idx = instruction.gpr_src(0).value();       // imr idx
            regidx_t gpr_mem_idx = instruction.gpr_src(1).number();  // src addr
            regidx_t src_mem_start = instruction.gpr_src(1).value(); // src addr in memory
            return make<aice_instruction_t>(make_loadscale_512(pc, gpr_imr, imr_idx, gpr_mem_idx, src_mem_start));
        }
        case opcode_t::LOADMX: {
            regidx_t gpr_imr = instruction.imr_dst(0).number();     // imr dst offset gpr
            regidx_t imr_idx = instruction.imr_dst(0).value();      // imr idx
            regidx_t gpr_mem_idx = instruction.mem_src(0).number(); // src addr
            addr_t src_mem_start = instruction.mem_src(0).pa();     // src addr in memory
            regidx_t gpr_stride = instruction.gpr_src(0).number();
            imm_t stride = instruction.gpr_src(0).value();
            regidx_t gpr_nrows = instruction.gpr_src(1).number(); // number of rows
            imm_t nrows = instruction.gpr_src(1).value();
            aice_emu::dtype_t dtype = static_cast<aice_emu::dtype_t>(instruction.dtype());
            const bool transpose = instruction.transpose();
            return make<aice_instruction_t>(make_loadmx(pc,
                                                        gpr_imr,
                                                        imr_idx,
                                                        gpr_mem_idx,
                                                        src_mem_start,
                                                        gpr_stride,
                                                        stride,
                                                        gpr_nrows,
                                                        nrows,
                                                        dtype,
                                                        transpose));
        }
        case opcode_t::LOADMXU_FP8: {
            regidx_t gpr_imr = instruction.imr_dst(0).number();     // imr dst offset gpr
            regidx_t imr_idx = instruction.imr_dst(0).value();      // imr idx
            regidx_t gpr_mem_idx = instruction.mem_src(0).number(); // src addr
            addr_t src_mem_start = instruction.mem_src(0).pa();     // src addr in memory
            aice_emu::dtype_t input_dtype = static_cast<aice_emu::dtype_t>(instruction.dtype());
            regidx_t gpr_stride = 0;
            imm_t stride = 0;
            regidx_t src_idx = 0;
            if (input_dtype == aice_emu::dtype_t::mxfp4_e2m1) {
                gpr_stride = instruction.gpr_src(0).number();
                stride = instruction.gpr_src(0).value();
                src_idx++;
            }
            regidx_t gpr_nrows = instruction.gpr_src(src_idx).number(); // number of rows gpr index
            imm_t nrows = instruction.gpr_src(src_idx).value();         // number of rows (after upconvert)
            const aice_emu::dtype_t dtype = aice_emu::dtype_t::mxfp8_e4m3;

            return make<aice_instruction_t>(make_loadmxu_fp8(
                pc, gpr_imr, imr_idx, gpr_mem_idx, src_mem_start, gpr_stride, stride, gpr_nrows, nrows, dtype));
        }
        case opcode_t::LOADMXU_BF16: {
            regidx_t gpr_imr = instruction.imr_dst(0).number();     // imr dst offset gpr
            regidx_t imr_idx = instruction.imr_dst(0).value();      // imr idx
            regidx_t gpr_mem_idx = instruction.mem_src(0).number(); // src addr
            addr_t src_mem_start = instruction.mem_src(0).pa();     // src addr in memory
            // aice_emu::dtype_t input_dtype = static_cast<aice_emu::dtype_t>(instruction.dtype());
            regidx_t gpr_stride = instruction.gpr_src(0).number();
            imm_t stride = instruction.gpr_src(0).value();
            regidx_t src_idx = 1;

            regidx_t gpr_nrows = instruction.gpr_src(src_idx).number(); // number of rows gpr index
            imm_t nrows = instruction.gpr_src(src_idx).value();         // number of rows (after upconvert)
            const aice_emu::dtype_t dtype = aice_emu::dtype_t::bf16;

            return make<aice_instruction_t>(make_loadmxu_bf16(
                pc, gpr_imr, imr_idx, gpr_mem_idx, src_mem_start, gpr_stride, stride, gpr_nrows, nrows, dtype));
        }
        case opcode_t::LOADIMR: {
            regidx_t gpr_imr = instruction.imr_dst(0).number();     // imr dst offset gpr
            regidx_t imr_idx = instruction.imr_dst(0).value();      // imr idx
            regidx_t gpr_mem_idx = instruction.mem_src(0).number(); // src addr
            regidx_t src_mem_start = instruction.mem_src(0).pa();   // src addr in memory
            regidx_t gpr_stride = instruction.gpr_src(0).number();
            regidx_t stride = instruction.gpr_src(0).value();
            regidx_t gpr_nrows = instruction.gpr_src(1).number(); // number of rows
            regidx_t nrows = instruction.gpr_src(1).value();
            aice_emu::dtype_t dtype = static_cast<aice_emu::dtype_t>(instruction.dtype());
            bool transpose = instruction.transpose();
            return make<aice_instruction_t>(make_loadimr(pc,
                                                         gpr_imr,
                                                         imr_idx,
                                                         gpr_mem_idx,
                                                         src_mem_start,
                                                         gpr_stride,
                                                         stride,
                                                         gpr_nrows,
                                                         nrows,
                                                         dtype,
                                                         transpose));
        }
        case opcode_t::MOVB: {
            regidx_t gpr_mar = instruction.breg_dst(0).number();
            regidx_t mar_idx = instruction.breg_dst(0).value();
            regidx_t gpr_imr = instruction.imr_src(0).number();
            regidx_t imr_idx = instruction.imr_src(0).value();
            return make<aice_instruction_t>(make_movb(pc, gpr_mar, mar_idx, gpr_imr, imr_idx));
        }
        case opcode_t::MATMULA: {
            regidx_t gpr_amr = instruction.amr_dst(0).number();
            regidx_t amr_idx = instruction.amr_dst(0).value();
            regidx_t gpr_imr = instruction.imr_src(0).number();
            regidx_t imr_idx = instruction.imr_src(0).value();
            regidx_t gpr_mar = instruction.breg_src(0).number();
            regidx_t mar_idx = instruction.breg_src(0).value();
            regidx_t gpr_kreg = instruction.gpr_src(0).number();
            regidx_t kreg_idx = instruction.gpr_src(0).value();
            return make<aice_instruction_t>(
                make_matmula(pc, gpr_amr, amr_idx, gpr_imr, imr_idx, gpr_mar, mar_idx, gpr_kreg, kreg_idx));
        }
        default:
            THROW("Unhandled AICE instruction {}", static_cast<unsigned>(instruction.opcode()));
        } // switch
    }

    aice_instruction_t get_next_instruction()
    {
        static std::uint64_t pc{0};

        if (!trc_reader) {
            std::string fn = input_trace;
            trc_reader = std::make_unique<is::InstructionStreamReader>(fn);
        }
        is::Event event;
        while (!done && trc_reader->read(event)) {
            if (event.has_instruction()) {
                const is::Instruction& instruction = event.instruction();
                // make an aice_instruction_t from the is::Instruction
                pc = instruction.program_counter().pa();
                DBG("Reading trace instr at pc: {}", pc);
                return make_aice_instr(instruction);
            } else {
                event.Clear();
                continue;
            }
        }
        done = true;
        ++pc;
        return make<aice_instruction_t>(make_synchalt(pc));
    }

    void emit_fetch_event(const const_aice_instruction_t& txn)
    {
        fetch_event(txn, evd_inum(txn->inum), evd_fetch_disasm(txn->disasm()));
    }
};

class IssueRec
{
    bool can_read{true};
    bool can_write{true};
    // AMRs stay busy until the final group write is done to prevent storeamr from reading it too soon.
    // but we need to be able to start another matmula to the same AMR before that time.
    // So amr_overwrite specifies earliest we can start a new matmul overwriting this AMR.
    sc::Cycle amr_overwrite{0};
    sc::Cycle mar_breg_overwrite{0};

public:
    // Check to delay younger read after older pending write.
    bool is_readable() const { return can_read; }
    void set_writer_pending() { can_read = false; }
    void set_writer_done() { can_read = true; }

    // Check to delay younger write while older pending read is occurring.
    // Only used for the matrix registers that have multi-cycle reads.
    bool is_writable() const { return can_write; }
    void set_reader_pending() { can_write = false; }
    void set_reader_done() { can_write = true; }

    void set_amr_overwrite(sc::Cycle time) { amr_overwrite = time; }
    bool matmula_can_overwrite(sc::Cycle now) const { return amr_overwrite <= now; }
    void set_mar_breg_overwrite(sc::Cycle time) { mar_breg_overwrite = time; }
    bool movb_can_overwrite(sc::Cycle now) const { return mar_breg_overwrite <= now; }
};
constexpr static size_t MAX_MAR_ROWS = 64;
constexpr static size_t MAX_MAR_COLS = 64;
constexpr static size_t MAX_IMR_ROWS = 64;
constexpr static size_t MAX_SPLIT_MAR_COLS = (MAX_MAR_COLS / 2);
constexpr static size_t MAX_MAR_SUPER_ROWS = (MAX_MAR_ROWS / 4);
constexpr static size_t MAX_MAR_SUPER_COLS = (MAX_MAR_COLS / 4);
constexpr static size_t MAX_GPRS = 64;
constexpr static size_t MAX_BREGS = 2;
constexpr static size_t MAX_IMRS = 32;
constexpr static size_t MAX_AMRS = 64;

struct MetaData
{
    std::array<unsigned, MAX_IMRS> imr_active_rows;
    std::array<aice_emu::dtype_t, MAX_IMRS> imr_dtype;
    std::array<bool, MAX_IMRS> imr_transpose;
    std::array<unsigned, MAX_IMRS> imr_mx_scales; // 4 or 8

    std::array<unsigned, MAX_BREGS> mar_active_cols;
    std::array<aice_emu::dtype_t, MAX_BREGS> mar_dtype;

    std::array<unsigned, MAX_AMRS> amr_active_rows;
    std::array<unsigned, MAX_AMRS> amr_active_cols;
};

struct wb_t
{
    regidx_t idx;
    aice_instruction_t inst;
};

static inline register_t radd(register_t r, std::uint32_t v)
{
    return static_cast<register_t>(std::to_underlying(r) + v);
}

class Decode : public sc::Module
{
public:
    sc::In<bool> load_queue_has_space_in{"load_queue_has_space_in"};   // from execute
    sc::In<bool> store_queue_has_space_in{"store_queue_has_space_in"}; // from execute
    sc::In<bool> load_queue_empty_in{"load_queue_empty_in"};           // from execute
    sc::In<bool> store_queue_empty_in{"store_queue_empty_in"};         // from execute
    sc::In<bool> tmem_fsms_active_in{"tmem_fsms_active_in"};           // from execute

    sc::In<aice_instruction_t> in{"in"};
    sc::Out<aice_instruction_t> out{"out"};
    sc::In<aice_instruction_t> writeback_in{"wb_in"};    // from execute
    sc::Out<aice_instruction_t> writeback_out{"wb_out"}; // to commit
    sc::Out<sc::Credit> decoder_credit_release_out{"decoder_credit_release_out"};
    sc::In<wb_t> amr_read_done_in{"amr_read_done_in"};               // from execute
    sc::In<wb_t> imr_read_done_in{"imr_read_done_in"};               // from execute
    sc::In<wb_t> imr_write_done_in{"imr_write_done_in"};             // from execute
    sc::In<wb_t> mar_write_done_in{"mar_write_done_in"};             // from execute
    sc::In<wb_t> imr_scale_write_done_in{"imr_scale_write_done_in"}; // from execute

    MetaData* metadata{nullptr};
    void set_metadata(MetaData* _metadata) { metadata = _metadata; }
    explicit Decode(const sc_core::sc_module_name& name) : sc::Module(name)
    {
        for (size_t idx = 0; idx < static_cast<size_t>(util::enum_size<resource_t>()); ++idx) {
            std::string name{util::enum_to_string<resource_t>(static_cast<resource_t>(idx))};
            ev_resources[idx] =
                new sc::Event(std::format("in_use_{}", name), std::format("resource in use event for {}", name));
        }
    }

    void end_of_elaboration() override
    {
        AMPERE_ASSERT(max_gprs <= MAX_GPRS, "Max gprs limit exceeded {} > {}", max_gprs, MAX_GPRS);
        AMPERE_ASSERT(max_imrs <= MAX_IMRS, "Max imrs limit exceeded {} > {}", max_imrs, MAX_IMRS);
        AMPERE_ASSERT(max_amrs <= MAX_AMRS, "Max amrs limit exceeded {} > {}", max_amrs, MAX_AMRS);
        AMPERE_ASSERT(max_bregs <= MAX_BREGS, "Max breg limit exceeded {} > {}", max_bregs, MAX_BREGS);
        for (auto [opcode, latency] : instruction_latencies.val()) {
            DBG("LATENCY: {}: {}", util::enum_to_string<opcode_t>(opcode), latency);
        }
    }

    void send_to_execute(const aice_instruction_t& txn)
    {
        out->put(txn); // send to execution
    }

    unsigned supercell_latency(const const_aice_instruction_t& txn) const
    {
        AMPERE_ASSERT(txn->is_matmula(), "Only for matmula instr");
        if (txn->dtype == aice_emu::dtype_t::fp32) { // matmula fp override for integer inputs
            return supercell_latency_fp;
        }
        const aice_emu::dtype_t dtype = metadata->imr_dtype.at(txn->matmul_imr_src()); // only looking at one operand
        switch (dtype) {
        case aice_emu::dtype_t::bf16:
        case aice_emu::dtype_t::fp8_e5m2:
        case aice_emu::dtype_t::fp8_e4m3:
            return supercell_latency_fp;
        case aice_emu::dtype_t::s8:
        case aice_emu::dtype_t::u8:
            return supercell_latency_int;
        default: // all mx types are fp
            if (is_mx(dtype)) {
                return supercell_latency_fp;
            }
        }
        THROW("Unexpected inum: {}, opcode: {}, dtype: {}",
              txn->inum,
              util::enum_to_string<opcode_t>(txn->opcode),
              aice_emu::dtype_t2str(dtype));
    }

    unsigned get_mar_latency(const aice_instruction_t& inst) const
    {
        // this is the occupancy of the "A-bus" feeding the MAR for MATMULA
        unsigned arows{0};
        switch (inst->opcode) {
        case opcode_t::MATMULA:
            arows = metadata->imr_active_rows.at(inst->matmul_imr_src());
            AMPERE_ASSERT(arows <= MAX_IMR_ROWS, "Too many rows for matmula");
            return arows;
        default:
            return 0;
        }
    }

    unsigned get_matmul_arows(const const_aice_instruction_t& inst) const
    {
        const unsigned arows = metadata->imr_active_rows.at(inst->matmul_imr_src());
        AMPERE_ASSERT(arows <= MAX_IMR_ROWS, "Too many rows for matmula");
        return arows;
    }
    unsigned get_latency_to_last_overwrite_of_first_output_group(const const_aice_instruction_t& inst) const
    {
        const unsigned vert = 16;
        const unsigned supercell = supercell_latency(inst);
        const unsigned arows = metadata->imr_active_rows.at(inst->matmul_imr_src());
        AMPERE_ASSERT(arows <= MAX_IMR_ROWS, "Too many rows for matmula");
        const unsigned latency = arows + vert + supercell;
        return latency;
    }

    unsigned get_latency(const const_aice_instruction_t& inst) const
    {
        unsigned latency{0};
        const unsigned vert = 16;
        unsigned arows{0};
        unsigned bcols{0};
        unsigned bscols{0};
        unsigned supercell{0};
        switch (inst->opcode) {
        case opcode_t::MOVB:
            bcols = metadata->imr_active_rows.at(inst->movb_src_imr());
            AMPERE_ASSERT(bcols <= MAX_MAR_COLS, "Too many cols for MOVB");
            latency = bcols;
            break;
        case opcode_t::MATMULA:
            supercell = supercell_latency(inst);
            arows = metadata->imr_active_rows.at(inst->matmul_imr_src());
            AMPERE_ASSERT(arows <= MAX_IMR_ROWS, "Too many rows for matmula");
            bcols = metadata->imr_active_rows.at(inst->matmul_mar_src());
            AMPERE_ASSERT(bcols <= MAX_MAR_COLS, "Too many cols for matmula");
            bscols = (bcols + 3) / 4; // roundup, convert to supercells
            DBG("matmula latency: arows: {}, bcols: {}, bscols: {}, vert: {}, supercell: {}",
                arows,
                bcols,
                bscols,
                vert,
                supercell);
            latency = arows + bscols + vert + supercell;
            break;
        case opcode_t::STOREAMR64:
        case opcode_t::STOREAMR32:
        case opcode_t::STOREAMR16:
        case opcode_t::STOREAMR8:
        case opcode_t::STOREAMRV:
        case opcode_t::STOREIMR:
            latency = 0; // runs at whatever rate the FSM sends the stores out
            break;
        case opcode_t::LOADIMR:
        case opcode_t::LOADMX:
        case opcode_t::LOADMXU_FP8:
        case opcode_t::LOADMXU_BF16:
        case opcode_t::LOADSCALE_128:
        case opcode_t::LOADSCALE_256:
        case opcode_t::LOADSCALE_512:
            // runs at whatever rate responses arrive to loadq and
            // loadimr transposes have a chunking into groups of 4
            latency = 0;
            break;
        default:
            latency = instruction_latencies.val().at(inst->opcode);
            break;
        }
        return latency;
    }

    bool load_queue_has_space{false};
    bool store_queue_has_space{false};
    bool load_queue_empty{false};
    bool store_queue_empty{false};
    bool tmem_fsms_active{false};

    void emit_decode_event(const const_aice_instruction_t& txn) { ev_decode_event(txn); }
    void emit_writeback_event(const const_aice_instruction_t& txn) { ev_writeback_event(txn); }

    bool run() override
    { // Decode::run()
        now = get_cycle();
        // get inputs from Execute
        if (tmem_fsms_active_in->can_take()) {
            tmem_fsms_active = tmem_fsms_active_in->take();
        }
        if (load_queue_has_space_in->can_take()) {
            load_queue_has_space = load_queue_has_space_in->take();
        }
        if (store_queue_has_space_in->can_take()) {
            store_queue_has_space = store_queue_has_space_in->take();
        }
        if (load_queue_empty_in->can_take()) {
            load_queue_empty = load_queue_empty_in->take();
        }
        if (store_queue_empty_in->can_take()) {
            store_queue_empty = store_queue_empty_in->take();
        }

        writebacks();
        if (in->can_take()) {
            aice_instruction_t txn = in->take();
            dec_txns.push_back(txn);
        }
        DBG("Decodes pending {}", dec_txns.size());
        bool issued = false;
        if (!issued && !dec_txns.empty()) {
            aice_instruction_t txn = dec_txns.at(0);
            const unsigned latency = get_latency(txn);
            txn->set_latency(latency);
            if (txn->is_matmula()) {
                const unsigned mar_latency = get_mar_latency(txn);
                txn->set_mar_latency(mar_latency);
                const unsigned output_latency = get_latency_to_last_overwrite_of_first_output_group(txn);
                txn->set_matmula_latency_to_last_overwrite_of_first_output_group(output_latency);
            }
            emit_decode_event(txn);
            DBG("Decode  inum: {} {:#x} {} // latency: {}", txn->inum, txn->pc, txn->disasm(), latency);
            unavailable_src_registers.clear();
            unavailable_dst_registers.clear();
            const bool src_rdy = sources_ready(txn);
            const bool res_rdy = resources_ready(txn);
            if (src_rdy && res_rdy) {
                dec_txns.pop_front();
                if (!txn->is_branch()) {
                    release_credit_to_fetch(txn);
                }
                claim_resources(txn);
                setup_inputs(txn);
                setup_outputs(txn);
                send_to_execute(txn);
            } else {
                if (!src_rdy) {
                    ev_decode_register_not_ready(txn,
                                                 evd_src_regs(regset_to_string(unavailable_src_registers)),
                                                 evd_dst_regs(regset_to_string(unavailable_dst_registers)));
                }
                if (!res_rdy) {
                    ev_decode_resource_unavailable(txn);
                }
                DBG("sources ready: {}, resources_ready: {}", src_rdy, res_rdy);
            }
        }
        events_for_used_resources();
        clean_resource_table();
        return dec_txns.size() || !resource_busy.empty();
    } // run

private:
    static std::string regset_to_string(std::unordered_set<register_t> iset)
    {
        std::string out;
        bool first = true;
        for (const auto& vv : iset) {
            if (first) {
                first = false;
            } else {
                out += ", ";
            }
            out += util::enum_to_string<register_t>(vv);
        }
        return out;
    }

    sc::Cycle now{0};
    std::deque<aice_instruction_t> dec_txns; // the work-in-progress, if any

    sc::Param<std::unordered_map<opcode_t, unsigned>> instruction_latencies{
        "instruction_latencies", "Instruction latencies", "[]"_json};
    sc::Param<unsigned> supercell_latency_int{"supercell_latency_int", "Supercell latency for INT ops", 1};
    sc::Param<unsigned> supercell_latency_fp{"supercell_latency_fp", "Supercell latency for FP ops", 4};

    sc::Param<unsigned> max_gprs{"max_gprs", "Max number of GPRS", 64};
    sc::Param<unsigned> max_imrs{"max_imrs", "Max number of IMRS", 32};
    sc::Param<unsigned> max_amrs{"max_amrs", "Max number of AMRS", 64};
    sc::Param<unsigned> max_bregs{"max_bregs", "Max number of MAR 'B' regs", 2};

    // track writers to the various register resources
    std::array<IssueRec, MAX_BREGS> breg_ir;
    std::array<IssueRec, MAX_GPRS> gpr_ir;
    std::array<IssueRec, MAX_IMRS> imr_ir;
    std::array<IssueRec, MAX_IMRS> scale_ir;
    std::array<IssueRec, MAX_AMRS> amr_ir;

    std::array<sc::Event*, util::enum_size<resource_t>()> ev_resources;
    sc::EventData<unsigned> evd_resource_used{"resource_used", "Resource used"};
    sc::Event ev_decode_event{"decode", "Decode instruction"};
    sc::Event ev_decode_resource_unavailable{"resource_unavailable", "Decode resource not available"};
    std::unordered_set<register_t> unavailable_src_registers;
    std::unordered_set<register_t> unavailable_dst_registers;
    sc::Event ev_decode_register_not_ready{"register_not_ready", "Decode register not ready"};
    sc::EventData<std::string> evd_src_regs{"src_registers", "Source Registers"};
    sc::EventData<std::string> evd_dst_regs{"dst_registers", "Destination Registers"};
    sc::Event ev_writeback_event{"writeback", "Writeback instruction data"};

    bool use_load_queue(const const_aice_instruction_t& inst) const
    {
        const bool use_loadq = (inst->opcode == opcode_t::LOADIMR || inst->opcode == opcode_t::LOADMX
                                || inst->opcode == opcode_t::LOADMXU_FP8 || inst->opcode == opcode_t::LOADMXU_BF16
                                || inst->opcode == opcode_t::SYNCREADDONE);
        return use_loadq;
    }
    bool use_store_queue(const const_aice_instruction_t& inst) const
    {
        switch (inst->opcode) {
        case opcode_t::STOREAMRV:
        case opcode_t::STOREAMR64:
        case opcode_t::STOREAMR32:
        case opcode_t::STOREAMR16:
        case opcode_t::STOREAMR8:
        case opcode_t::STOREIMR:
        case opcode_t::SYNCWRITEDONE:
            return true;
        default:
            return false;
        }
    }

    void release_credit_to_fetch(aice_instruction_t& txn)
    {
        decoder_credit_release_out->put(std::move(txn->decoder_credit));
        txn->decoder_credit = nullptr;
    }

    void events_for_used_resources()
    {
        for (size_t idx = 1; idx < static_cast<size_t>(util::enum_size<resource_t>()); ++idx) {
            resource_t res = static_cast<resource_t>(idx);
            if (const auto& txn = resource_used(res, now); txn) {
                (*ev_resources[idx])(txn, evd_resource_used(1));
            }
        }
    }

    aice_instruction_t resource_used(resource_t res, sc::Cycle time) const
    {
        if (const auto& search = resource_busy.find(time); search != resource_busy.end()) {
            const auto& rec = search->second;
            DBG("resource {} is {}",
                util::enum_to_string<resource_t>(res),
                rec[static_cast<size_t>(res)] ? "used" : "free");
            return rec[static_cast<size_t>(res)];
        }
        DBG("resource {} is free", util::enum_to_string<resource_t>(res));
        return nullptr;
    }

    bool resource_available(resource_t res, sc::Cycle time) const
    {
        if (const auto& search = resource_busy.find(time); search != resource_busy.end()) {
            const auto& rec = search->second;
            DBG("resource {} is {}",
                util::enum_to_string<resource_t>(res),
                rec[static_cast<size_t>(res)] ? "used" : "free");
            return !rec[static_cast<size_t>(res)];
        }
        DBG("resource {} is free", util::enum_to_string<resource_t>(res));
        return true;
    }
    void set_resource_used(aice_instruction_t& inst, resource_t res, sc::Cycle time)
    {
        DBG("set_resource_used {} @ {}", util::enum_to_string<resource_t>(res), time);
        if (auto search = resource_busy.find(time); search != resource_busy.end()) {
            auto& rec = search->second;
            AMPERE_ASSERT(rec[static_cast<size_t>(res)] == nullptr,
                          "Trying to use a resource that is already in use: {} at cycle {}",
                          util::enum_to_string<resource_t>(res),
                          time);
            rec[static_cast<size_t>(res)] = inst;
        } else {
            resource_tracker_t tracker;
            tracker[static_cast<size_t>(res)] = inst;
            resource_busy[time] = tracker;
        }
    }

    void clean_resource_table()
    {
        DBG("clean_resource_table");
        resource_busy.erase(now);
    }

    using resource_tracker_t = std::array<aice_instruction_t, util::enum_size<resource_t>()>;
    std::map<sc::Cycle, resource_tracker_t> resource_busy;

    bool resources_ready(const const_aice_instruction_t& inst) const
    {
        const auto range = required_resources.equal_range(inst->opcode);
        for (auto itr = range.first; itr != range.second; ++itr) {
            auto& res = itr->second;
            switch (res) {
            case resource_t::TMEM_FSMS: // for SYNCHALT
                if (!load_queue_empty || !store_queue_empty || tmem_fsms_active) {
                    return false;
                }
                break;
            case resource_t::WB:
                // Writeback resource occurs after execution
                if (!resource_available(res, now + inst->latency())) {
                    return false;
                }
                break;
            case resource_t::LOADQ:
                if (!load_queue_has_space) {
                    return false;
                }
                break;
            case resource_t::STOREQ:
                if (!store_queue_has_space) {
                    return false;
                }
                break;
            default:
                if (!resource_available(res, now)) {
                    return false;
                }
            }
        }
        return true;
    }
    void claim_resources(aice_instruction_t& inst)
    {
        const auto range = required_resources.equal_range(inst->opcode);
        for (auto itr = range.first; itr != range.second; ++itr) {
            auto& res = itr->second;
            switch (res) {
            case resource_t::TMEM_FSMS:
                break;
            case resource_t::WB:
                // Writeback resource occurs after execution
                set_resource_used(inst, res, now + inst->latency());
                break;
            case resource_t::MAR_BBUS:
                DBG("Claiming resource {} for {} cycles", util::enum_to_string<resource_t>(res), inst->latency());
                for (size_t idx = 0; idx < inst->latency(); ++idx) {
                    set_resource_used(inst, res, now + idx);
                }
                break;
            case resource_t::MAR: // use mar latency which is less than "latency" for matmul to allow stagger overlaps
                DBG("Claiming resource {} for {} cycles", util::enum_to_string<resource_t>(res), inst->mar_latency());
                for (size_t idx = 0; idx < inst->mar_latency(); ++idx) {
                    set_resource_used(inst, res, now + idx);
                }
                break;
            case resource_t::LOADQ:
                load_queue_has_space = false; // prevent sending 2 in 1 cycle
                break;
            case resource_t::STOREQ:
                store_queue_has_space = false; // prevent sending 2 in 1 cycle
                break;
            default:
                set_resource_used(inst, res, now);
            }
        }
    }

    bool sources_ready(const const_aice_instruction_t& inst)
    {
        bool retval = true;
        for (const auto& opnd : inst->operands) {
            if (opnd.read && !check_read(inst, opnd)) {
                retval = false;
                break;
            }
            if (opnd.written && !check_write(inst, opnd)) {
                retval = false;
                break;
            }
        }
        return retval;
    }
    bool check_read(const const_aice_instruction_t& inst, const aice_operand_t& opnd)
    {
        switch (opnd.op_type) {
        case operand_t::MAR_REG:
            if (!breg_ir[opnd.val].is_readable()) {
                unavailable_src_registers.insert(radd(register_t::mar0, opnd.val));
                return false;
            }
            break;
        case operand_t::GPR:
            if (!gpr_ir[opnd.gpr_idx].is_readable()) {
                unavailable_src_registers.insert(radd(register_t::r0, opnd.gpr_idx));
                return false;
            }
            break;
        case operand_t::GPR_IMR: // check the GPR and the IMR it points to
            if (!gpr_ir[opnd.gpr_idx].is_readable()) {
                unavailable_src_registers.insert(radd(register_t::r0, opnd.gpr_idx));
                return false;
            }
            if (!imr_ir[opnd.val].is_readable()) {
                unavailable_src_registers.insert(radd(register_t::imr0, opnd.val));
                return false;
            }
            if (is_mx(metadata->imr_dtype.at(opnd.val))) { // MOVB and MATMULTA need to check scales too for MX types
                if (!scale_ir[opnd.val].is_readable()) {
                    unavailable_src_registers.insert(radd(register_t::scale0, opnd.val));
                    return false;
                }
            }
            break;
        case operand_t::GPR_SCALE: // check the GPR and the IMR it points to
            if (!gpr_ir[opnd.gpr_idx].is_readable()) {
                unavailable_src_registers.insert(radd(register_t::r0, opnd.gpr_idx));
                return false;
            }
            if (!scale_ir[opnd.val].is_readable()) {
                unavailable_src_registers.insert(radd(register_t::scale0, opnd.gpr_idx));
                return false;
            }
            break;
        case operand_t::GPR_AMR: // check the GPR and the AMR it points to
            if (!gpr_ir[opnd.gpr_idx].is_readable()) {
                unavailable_src_registers.insert(radd(register_t::r0, opnd.gpr_idx));
                return false;
            }
            if (inst->is_matmula() && amr_ir[opnd.val].matmula_can_overwrite(now)) {
                ; // do nothing
            } else if (!amr_ir[opnd.val].is_readable()) {
                unavailable_src_registers.insert(radd(register_t::amr0, opnd.val));
                return false;
            }
            break;
        default:
            break;
        }
        return true;
    }

    bool check_write(const const_aice_instruction_t& inst, const aice_operand_t& opnd)
    {
        switch (opnd.op_type) {
        case operand_t::MAR_REG:
            if (inst->is_movb() && breg_ir[opnd.val].movb_can_overwrite(now)) {
                ; // do nothing
            } else if (!breg_ir[opnd.val].is_writable()) {
                unavailable_dst_registers.insert(radd(register_t::mar0, opnd.val));
                return false;
            }
            break;
        case operand_t::GPR:
#if defined(GPR_READ_CHECKS_ENABLED)
            if (!gpr_ir[opnd.gpr_idx].is_writable()) {
                unavailable_dst_registers.insert(radd(register_t::r0, opnd.gpr_idx));
                return false;
            }
#endif
            break;
        case operand_t::GPR_IMR:
            if (!gpr_ir[opnd.gpr_idx].is_readable()) { // make sure GPR is readable before checking IMR
                unavailable_dst_registers.insert(radd(register_t::r0, opnd.gpr_idx));
                return false;
            }
            if (!imr_ir[opnd.val].is_writable()) {
                unavailable_dst_registers.insert(radd(register_t::imr0, opnd.val));
                return false;
            }
            // this should never get used... only writer is via GPR_SCALE
            if (is_mx(metadata->imr_dtype.at(opnd.val))) { // MOVB and MATMULTA need to check scales too for MX types
                if (!scale_ir[opnd.val].is_writable()) {
                    unavailable_dst_registers.insert(radd(register_t::scale0, opnd.val));
                    return false;
                }
            }
            break;
        case operand_t::GPR_SCALE:
            if (!gpr_ir[opnd.gpr_idx].is_readable()) { // make sure GPR is readable before checking IMR
                unavailable_dst_registers.insert(radd(register_t::r0, opnd.gpr_idx));
                return false;
            }
            if (!scale_ir[opnd.val].is_writable()) {
                unavailable_dst_registers.insert(radd(register_t::scale0, opnd.val));
                return false;
            }
            break;
        case operand_t::GPR_AMR:                       // check the AMR only
            if (!gpr_ir[opnd.gpr_idx].is_readable()) { // make sure GPR is readable before checking IMR
                unavailable_dst_registers.insert(radd(register_t::r0, opnd.gpr_idx));
                return false;
            }
            if (inst->is_matmula() && amr_ir[opnd.val].matmula_can_overwrite(now)) {
                ; // do nothing
            } else if (!amr_ir[opnd.val].is_writable()) {
                unavailable_dst_registers.insert(radd(register_t::amr0, opnd.val));
                return false;
            }
            break;
        default:
            break;
        }
        return true;
    }

    void setup_outputs(const const_aice_instruction_t& inst)
    {
        for (const auto& opnd : inst->operands) {
            if (opnd.written) {
                switch (opnd.op_type) {
                case operand_t::MAR_REG:
                    DBG("GPR{}=MAR{} writer pending", opnd.gpr_idx, opnd.val);
                    breg_ir[opnd.val].set_writer_pending();
                    break;
                case operand_t::GPR:
                    // only block GPRs if being written by MUL/MULI;
                    // everything else that writes a GPR has single
                    // cycle and bypasses.
                    if (inst->is_mul()) {
                        if (opnd.gpr_idx) { // don't ever block on the zeroreg
                            DBG("GPR{} writer pending", opnd.gpr_idx);
                            gpr_ir[opnd.gpr_idx].set_writer_pending();
                        }
                    }
                    break;
                case operand_t::GPR_IMR:
                    DBG("GPR{}=IMR{} writer pending", opnd.gpr_idx, opnd.val);
                    imr_ir[opnd.val].set_writer_pending();
                    break;
                case operand_t::GPR_SCALE:
                    DBG("GPR{}=SCALE{} writer pending", opnd.gpr_idx, opnd.val);
                    scale_ir[opnd.val].set_writer_pending();
                    break;
                case operand_t::GPR_AMR:
                    DBG("GPR{}=AMR{} writer pending", opnd.gpr_idx, opnd.val);
                    amr_ir[opnd.val].set_writer_pending();
                    if (inst->is_matmula()) {
                        amr_ir[opnd.val].set_amr_overwrite(
                            now + inst->matmula_latency_to_last_overwrite_of_first_output_group());
                    }
                default:
                    continue;
                }
            }
        }
    }
    void setup_inputs(const const_aice_instruction_t& inst)
    {
        for (const auto& opnd : inst->operands) {
            if (opnd.read) {
                switch (opnd.op_type) {
                case operand_t::MAR_REG:
                    DBG("GPR{}=MAR{} reader pending", opnd.gpr_idx, opnd.val);
                    breg_ir[opnd.val].set_reader_pending();
                    if (inst->is_matmula()) {
                        breg_ir[opnd.val].set_mar_breg_overwrite(now + get_matmul_arows(inst));
                    }
                case operand_t::GPR:
#if defined(GPR_READ_CHECKS_ENABLED)
                    gpr_ir[opnd.gpr_idx].set_reader_pending();
#endif
                    break;
                case operand_t::GPR_IMR:
                    DBG("GPR{}=IMR{} reader pending", opnd.gpr_idx, opnd.val);
                    imr_ir[opnd.val].set_reader_pending();
                    if (is_mx(metadata->imr_dtype.at(
                            opnd.val))) { // MOVB and MATMULTA need to check scales too for MX types
                        DBG("GPR{}=SCALE{} reader pending", opnd.gpr_idx, opnd.val);
                        scale_ir[opnd.val].set_reader_pending();
                    }
                    break;
                case operand_t::GPR_SCALE:
                    DBG("GPR{}=SCALE{} reader pending", opnd.gpr_idx, opnd.val);
                    scale_ir[opnd.val].set_reader_pending();
                    break;
                case operand_t::GPR_AMR:
                    DBG("GPR{}=AMR{} reader pending", opnd.gpr_idx, opnd.val);
                    amr_ir[opnd.val].set_reader_pending();
                    break;
                default:
                    continue;
                }
            }
        }
    }

    void writeback_outputs(const const_aice_instruction_t& inst)
    {
        for (const auto& opnd : inst->operands) {
            if (opnd.written) {
                switch (opnd.op_type) {
                case operand_t::MAR_REG: // FIXME: still needed?
                    breg_ir[opnd.val].set_writer_done();
                    break;
                case operand_t::GPR:
                    gpr_ir[opnd.gpr_idx].set_writer_done();
                    break;
                case operand_t::GPR_IMR:
                case operand_t::GPR_SCALE:
                    // writes of thse are for LOADIMR, LOADMX,
                    // LOADMXU, LOADSCALE and those registers are set
                    // ready by the TMEM FSM.
                    break;
                case operand_t::GPR_AMR: // matmul will write this
                    writeback_amr(opnd.val);
                    break;
                default:
                    continue;
                }
            }
        }
    }

    void finish_reads(const const_aice_instruction_t& inst)
    {
        for (const auto& opnd : inst->operands) {
            if (opnd.read) {
                switch (opnd.op_type) {
                case operand_t::MAR_REG:
                    breg_ir[opnd.val].set_reader_done();
                    break;
                case operand_t::GPR:
#if defined(GPR_READ_CHECKS_ENABLED)
                    gpr_ir[opnd.gpr_idx].set_reader_done();
#endif
                    break;
                case operand_t::GPR_IMR:
                    finish_read_imr(opnd.val);
                    if (is_mx(metadata->imr_dtype.at(
                            opnd.val))) { // MOVB and MATMULTA need to check scales too for MX types
                        finish_read_scale(opnd.val);
                    }
                    break;
                case operand_t::GPR_SCALE:
                    finish_read_scale(opnd.val);
                    break;
                case operand_t::GPR_AMR:
                    finish_read_amr(opnd.val);
                    break;
                default:
                    continue;
                }
            }
        }
    }

    void finish_read_scale(regidx_t idx) { scale_ir[idx].set_reader_done(); }
    void finish_read_imr(regidx_t idx) { imr_ir[idx].set_reader_done(); }
    void finish_read_amr(regidx_t idx) { amr_ir[idx].set_reader_done(); }
    void writeback_imr(regidx_t idx)
    {
        DBG("writeback_imr {}", idx);
        imr_ir[idx].set_writer_done();
    }
    void writeback_mar(regidx_t idx)
    {
        DBG("writeback_mar {}", idx);
        breg_ir[idx].set_writer_done();
    }
    void writeback_imr_scale(regidx_t idx)
    {
        DBG("writeback_imr_scale {}", idx);
        scale_ir[idx].set_writer_done();
    }
    void writeback_amr(regidx_t idx) { amr_ir[idx].set_writer_done(); }

    sc::Event ev_done_read_scale{"done_read_scale", "Read scale done"};
    sc::Event ev_done_write_scale{"done_write_scale", "Write scale done"};
    sc::Event ev_done_read_imr{"done_read_imr", "Read IMR done"};
    sc::Event ev_done_write_imr{"done_write_imr", "Write IMR done"};
    sc::Event ev_done_read_amr{"done_read_amr", "Read AMR done"};
    sc::Event ev_done_write_amr{"done_write_amr", "Write AMR done"};
    sc::Event ev_done_read_mar{"done_read_mar", "Read MAR done"}; // FIXME: use this
    sc::Event ev_done_write_mar{"done_write_mar", "Write MAR done"};
    sc::EventData<register_t> evd_reg{"reg", "Register index"};

    void writebacks_amr()
    {
        while (amr_read_done_in->can_take()) {
            wb_t wb = amr_read_done_in->take();
            ev_done_read_amr(wb.inst, evd_reg(radd(register_t::amr0, wb.idx)));
            finish_read_amr(wb.idx);
        }
    }
    void writebacks_imr()
    {
        while (imr_read_done_in->can_take()) {
            wb_t wb = imr_read_done_in->take();
            ev_done_read_imr(wb.inst, evd_reg(radd(register_t::imr0, wb.idx)));
            finish_read_imr(wb.idx);
        }
        while (mar_write_done_in->can_take()) {
            wb_t wb = mar_write_done_in->take();
            ev_done_write_mar(wb.inst, evd_reg(radd(register_t::mar0, wb.idx)));
            writeback_mar(wb.idx);
        }
        while (imr_write_done_in->can_take()) {
            wb_t wb = imr_write_done_in->take();
            ev_done_write_imr(wb.inst, evd_reg(radd(register_t::imr0, wb.idx)));
            writeback_imr(wb.idx);
        }
        while (imr_scale_write_done_in->can_take()) {
            wb_t wb = imr_write_done_in->take();
            ev_done_write_imr(wb.inst, evd_reg(radd(register_t::scale0, wb.idx)));
            writeback_imr_scale(wb.idx);
        }
    }
    void writebacks()
    {
        writebacks_imr();
        writebacks_amr();
        while (writeback_in->can_take()) {
            auto txn = writeback_in->take();
            if (!use_load_queue(txn) && !use_store_queue(txn)) { // drop event for ld/st
                emit_writeback_event(txn);
                DBG("Writeback {} {:#x} {}", txn->inum, txn->pc, txn->disasm());
            }
            if (txn->is_branch()) {
                release_credit_to_fetch(txn);
            }
            writeback_outputs(txn);
            finish_reads(txn);
            writeback_out->put(txn);
        }
    }
};

struct amr_write_sequence_t
{
    std::uint32_t counter{0};
    regidx_t amr_idx{0};
    accumulate_option_t accumulate_option{accumulate_option_t::NONE};
    amr_write_sequence_t(regidx_t amr_idx, accumulate_option_t option) : amr_idx(amr_idx), accumulate_option(option) {}
};

class Execute : public sc::Module
{
    sc::Cycle now;
    std::deque<aice_instruction_t> load_queue;  // for deferred LOADIMR, LOADMX, LOADMXU, LOADSCALE, SYNCREADDONE
    std::deque<aice_instruction_t> store_queue; // for deferred STOREIMR, STOREAMR, SYNCWRITEDONE
    sc::Param<unsigned> load_queue_size{"load_queue_size", "Size of load queue", 8};
    sc::Param<unsigned> store_queue_size{"store_queue_size", "Size of store queue", 8};
    sc::Param<unsigned> store_delay{"store_delay", "Store delay (uarch)", 4};

public:
    MetaData* metadata{nullptr};
    void set_metadata(MetaData* _metadata) { metadata = _metadata; }

    explicit Execute(const sc_core::sc_module_name& name) : sc::Module(name) {}
    sc::Param<unsigned> amr_max_write_cycles{"amr_max_write_cycles", "AMR chunk write cycles", 43};

    // ports
    sc::In<aice_instruction_t> in{"in"};                                  // from decode
    sc::Out<aice_instruction_t> out{"out"};                               // to decode.writeback_in
    sc::Out<bool> load_queue_has_space_out{"load_queue_has_space_out"};   // to decode
    sc::Out<bool> store_queue_has_space_out{"store_queue_has_space_out"}; // to decode
    sc::Out<bool> load_queue_empty_out{"load_queue_empty_out"};           // to decode
    sc::Out<bool> store_queue_empty_out{"store_queue_empty_out"};         // to decode
    sc::Out<bool> tmem_fsms_active_out{"tmem_fsms_active_out"};           // to decode
    sc::Out<wb_t> amr_read_done_out{"amr_read_done_out"};
    sc::Out<wb_t> imr_read_done_out{"imr_read_done_out"};
    sc::Out<wb_t> mar_write_done_out{"mar_write_done_out"};
    sc::Out<wb_t> imr_write_done_out{"imr_write_done_out"};
    sc::Out<wb_t> imr_scale_write_done_out{"imr_scale_write_done_out"};
    sc::Out<mem_req_t> mem_req_port{"mem_request"};
    sc::In<mem_resp_t> mem_resp_port{"mem_response"};

    sc::Event ev_load_queue{"load_queue", "load queue"};
    sc::Event ev_store_queue{"store_queue", "store queue"};
    void emit_execute_event(const const_aice_instruction_t& txn) { execute_event(txn); }
    void emit_load_queue_event(const const_aice_instruction_t& txn) { ev_load_queue(txn); }
    void emit_store_queue_event(const const_aice_instruction_t& txn) { ev_store_queue(txn); }

    void propagate_meta_data(aice_instruction_t& txn)
    {
        // propagate the metadata:
        //    from the instruction to the IMR/MAR
        //    from the IMR to the MAR
        //    from the MAR & IMR to AMR
        switch (txn->opcode) {
        case opcode_t::LOADIMR:
            metadata->imr_dtype.at(txn->loadimr_dst_imr()) = txn->loadimr_dtype();
            metadata->imr_transpose.at(txn->loadimr_dst_imr()) = txn->is_transpose();
            metadata->imr_active_rows.at(txn->loadimr_dst_imr()) = txn->loadimr_nrows();
            metadata->imr_mx_scales.at(txn->loadimr_dst_imr()) = 0;
            break;
        case opcode_t::LOADMX:
            metadata->imr_dtype.at(txn->loadimr_dst_imr()) = txn->dtype;
            metadata->imr_transpose.at(txn->loadimr_dst_imr()) = false;
            switch (txn->dtype) {
            case aice_emu::dtype_t::mxint4:
            case aice_emu::dtype_t::mxfp4_e2m1:
                metadata->imr_mx_scales.at(txn->loadimr_dst_imr()) = 8;
                break;
            default:
                metadata->imr_mx_scales.at(txn->loadimr_dst_imr()) = 4;
                break;
            }
            break;
        case opcode_t::LOADMXU_FP8:
            metadata->imr_dtype.at(txn->loadimr_dst_imr()) = aice_emu::dtype_t::mxfp8_e4m3;
            metadata->imr_mx_scales.at(txn->loadimr_dst_imr()) = 4;
            metadata->imr_transpose.at(txn->loadimr_dst_imr()) = false;
            break;
        case opcode_t::LOADMXU_BF16:
            metadata->imr_dtype.at(txn->loadimr_dst_imr()) = aice_emu::dtype_t::bf16;
            metadata->imr_mx_scales.at(txn->loadimr_dst_imr()) = 2;
            metadata->imr_transpose.at(txn->loadimr_dst_imr()) = false;
            break;
        case opcode_t::MOVB:
            metadata->mar_active_cols.at(txn->dst_mar_reg()) = metadata->imr_active_rows.at(txn->movb_src_imr());
            metadata->mar_dtype.at(txn->dst_mar_reg()) = metadata->imr_dtype.at(txn->movb_src_imr());
            break;
        case opcode_t::MATMULA:
            metadata->amr_active_rows.at(txn->matmul_amr_dest()) = metadata->imr_active_rows.at(txn->matmul_imr_src());
            metadata->amr_active_cols.at(txn->matmul_amr_dest()) = metadata->mar_active_cols.at(txn->matmul_mar_src());
            break;
        default:
            break;
        }
    }

    bool youngest_load_fsm_done_making_requests() const
    {
        if (!tmem_load_fsms.empty()) {
            return tmem_load_fsms.back()->done_sending_requests;
        }
        return true;
    }
    bool youngest_store_fsm_done_making_requests() const
    {
        if (!tmem_store_fsms.empty()) {
            return tmem_store_fsms.back()->done_sending_requests;
        }
        return true;
    }

    bool youngest_load_fsm_done_receiving_replies() const
    {
        if (!tmem_load_fsms.empty()) {
            return tmem_load_fsms.back()->done_receiving_responses;
        }
        return true;
    }
    bool youngest_store_fsm_done_receiving_replies() const
    {
        if (!tmem_store_fsms.empty()) {
            return tmem_store_fsms.back()->done_receiving_responses;
        }
        return true;
    }
    bool run() override
    { // Execute::run()
        now = get_cycle();

        for (auto txn : load_queue) {
            emit_load_queue_event(txn);
        }
        for (auto txn : store_queue) {
            emit_store_queue_event(txn);
        }

        std::vector<aice_instruction_t> wb;
        while (in->can_take()) { // from decode
            aice_instruction_t txn = in->take();
            propagate_meta_data(txn);
            emit_execute_event(txn);
            DBG("Execute {} {:#x} {}", txn->inum, txn->pc, txn->disasm());

            const auto latency = txn->latency();
            switch (txn->opcode) {
            case opcode_t::DEBUG: {
                ev_debug(txn);
            } break;
            case opcode_t::RESTARTABLE:
                break;
            case opcode_t::SYNCWAIT: {
                ev_syncwait(txn);
            } break;
            case opcode_t::LOADSCALE_128:
            case opcode_t::LOADSCALE_256:
            case opcode_t::LOADSCALE_512:
            case opcode_t::SYNCREADDONE:
            case opcode_t::LOADMX:
            case opcode_t::LOADMXU_FP8:
            case opcode_t::LOADMXU_BF16:
            case opcode_t::LOADIMR:
                DBG("\tadd to loadq {} {:#x} {}", txn->inum, txn->pc, txn->disasm());
                load_queue.push_back(txn);
                emit_load_queue_event(txn);
                break;
            case opcode_t::SYNCWRITEDONE:
            case opcode_t::STOREIMR:
            case opcode_t::STOREAMRV:
            case opcode_t::STOREAMR64:
            case opcode_t::STOREAMR32:
            case opcode_t::STOREAMR16:
            case opcode_t::STOREAMR8:
                DBG("\tadd to storeq {} {:#x} {}", txn->inum, txn->pc, txn->disasm());
                store_queue.push_back(txn);
                emit_store_queue_event(txn);
                break;
            case opcode_t::MOVB:
                if (latency > 1) {
                    // subtract one to account for execution this cycle
                    xu_pipeline.emplace(now + latency - 1, txn);
                } else {
                    wb.push_back(txn);
                }
                movb_instr = txn;
                movb_in_progress++;
                break;
            case opcode_t::MATMULA:
                // 8+4ish (for super cell pipeline) cycles after starting:
                // Send 7 cycles of ramp up columns 0..6 + 25 cycles of 8 chunks + 7 cycles of ramp down (columns 1..7)
                // How to make it generic based on 32 cycle latency?

                if (latency > 1) {
                    // subtract one to account for execution this cycle
                    xu_pipeline.emplace(now + latency - 1, txn);

                    const regidx_t amr_dest = txn->matmul_amr_dest();

                    accumulate_option_t accumulate_option = accumulate_option_t::NONE; // FIMXE
                    amr_write_sequence_t amr_write_sequence(amr_dest, accumulate_option);
                } else {
                    wb.push_back(txn);
                }
                matmul_instr.insert(txn);
                matmul_in_progress++;
                add_mar_activity_txn(txn);
                break;
            default:
                if (latency > 1) {
                    // subtract one to account for execution this cycle
                    xu_pipeline.emplace(now + latency - 1, txn);
                } else {
                    wb.push_back(txn);
                }
                break;
            }
        }

        if (load_queue.size() && youngest_load_fsm_done_making_requests()) {
            auto txn = load_queue.at(0);
            bool pop = true;
            if (txn->opcode == opcode_t::SYNCREADDONE && !youngest_load_fsm_done_receiving_replies()) {
                pop = false;
            }
            if (pop) {
                load_queue.pop_front();
                switch (txn->opcode) {
                case opcode_t::LOADSCALE_128:
                    create_tmem_fsm_for_load_scale(txn, 2);
                    break;
                case opcode_t::LOADSCALE_256:
                    create_tmem_fsm_for_load_scale(txn, 4);
                    break;
                case opcode_t::LOADSCALE_512:
                    create_tmem_fsm_for_load_scale(txn, 8);
                    break;
                case opcode_t::LOADMX:
                case opcode_t::LOADMXU_FP8:
                case opcode_t::LOADMXU_BF16:
                case opcode_t::LOADIMR:
                    create_tmem_fsm_for_load(txn);
                    break;
                case opcode_t::SYNCREADDONE:
                    ev_syncreaddone(txn);
                    done_execute(txn);
                    break;
                default:
                    THROW("Unexpected instr in loadq {}", util::enum_to_string<opcode_t>(txn->opcode));
                }
            }
        }
        if (store_queue.size() && youngest_store_fsm_done_making_requests()) {
            auto txn = store_queue.at(0);
            bool pop = true;
            if (txn->opcode == opcode_t::SYNCWRITEDONE && !youngest_store_fsm_done_receiving_replies()) {
                pop = false;
            }
            if (pop) {
                store_queue.pop_front();
                switch (txn->opcode) {
                case opcode_t::STOREIMR:
                    create_tmem_fsm_for_store_imr(txn);
                    break;
                case opcode_t::STOREAMR64:
                    create_tmem_fsm_for_store_amr(txn, 64);
                    break;
                case opcode_t::STOREAMR32:
                    create_tmem_fsm_for_store_amr(txn, 32);
                    break;
                case opcode_t::STOREAMR16:
                    create_tmem_fsm_for_store_amr(txn, 16);
                    break;
                case opcode_t::STOREAMR8:
                    create_tmem_fsm_for_store_amr(txn, 8);
                    break;
                case opcode_t::STOREAMRV:
                    create_tmem_fsm_for_store_amr_vector(txn);
                    break;
                case opcode_t::SYNCWRITEDONE:
                    ev_syncwritedone(txn);
                    done_execute(txn);
                    break;
                default:
                    THROW("Unexpected instr in storeq {}", util::enum_to_string<opcode_t>(txn->opcode));
                }
            }
        }

        for (auto txn : wb) { // single cycle latency stuff gets in and out same cycle
            done_execute(txn);
        }
        // running events
        for (const auto& mminst : matmul_instr) {
            emit_matmul_event(mminst);
        }
        pop_mar_activity_tracker();
        if (movb_instr) {
            emit_movb_event(movb_instr);
        }

        // write back (non-LOADIMR) stuff that finishes executing
        const auto range = xu_pipeline.equal_range(now);
        for (auto itr = range.first; itr != range.second; ++itr) {
            auto& txn = itr->second;
            done_execute(txn);
            switch (txn->opcode) {
            case opcode_t::MATMULA:
                AMPERE_ASSERT(matmul_in_progress > 0, "Must have a matmula in progress to decrement");
                matmul_in_progress--;
                matmul_instr.erase(txn);
                break;
            case opcode_t::MOVB:
                AMPERE_ASSERT(movb_in_progress > 0, "Must have a movb in progress to decrement");
                movb_in_progress--;
                if (movb_in_progress == 0) {
                    movb_instr = nullptr;
                }
                break;
            default:
                break;
            }
        }
        xu_pipeline.erase(now);
        process_tmem_writes();
        amr_write_generator.erase(now);

        tmem_fsms_step();

        // talk to decode about the state of the load and store queue
        load_queue_has_space_out->put(load_queue.size() < load_queue_size);
        store_queue_has_space_out->put(store_queue.size() < store_queue_size);
        load_queue_empty_out->put(load_queue.empty());
        store_queue_empty_out->put(store_queue.empty());
        tmem_fsms_active_out->put(!tmem_load_fsms.empty() || !tmem_store_fsms.empty());

        return !xu_pipeline.empty() || !tmem_load_fsms.empty() || !tmem_store_fsms.empty()
               || !amr_write_generator.empty() || !load_queue.empty() || !store_queue.empty();
    }

    sc::Event amr_chunk_write{"amr_chunk_write", "AMR chunk write"};
    sc::EventData<unsigned> evd_cycle{"cycle", "AMR chunk"};

    void process_tmem_writes()
    {
        const auto range = amr_write_generator.equal_range(now);
        for (auto itr = range.first; itr != range.second; ++itr) {
            auto& aws = itr->second; // copy
            // FIXME: emit events
            ++aws.counter;
            if (aws.counter < amr_max_write_cycles) {
                // insert in amr_write_generator for future cycle
                amr_write_generator.emplace(now + 1, aws);
            }
        }
    }

private:
    // for event reporting
    std::unordered_set<aice_instruction_t> matmul_instr;
    aice_instruction_t movb_instr{nullptr};
    unsigned matmul_in_progress{0};
    unsigned movb_in_progress{0};

    sc::Event execute_event{"execute", "Execute instruction"};
    void emit_matmul_event(const const_aice_instruction_t& txn)
    {
        ev_matmul(txn, evd_supercell_count(supercell_count()));
    }
    void emit_movb_event(const const_aice_instruction_t& txn) { ev_movb(txn); }

    std::deque<int> mar_activity;

    void add_mar_activity_txn(aice_instruction_t& txn)
    {
        unsigned arows = metadata->imr_active_rows.at(txn->matmul_imr_src());
        unsigned ncols = metadata->mar_active_cols.at(txn->matmul_mar_src());

        AMPERE_ASSERT(arows > 0 && arows <= 64, "AROWS must be in range");
        AMPERE_ASSERT(ncols > 0 && ncols <= 64, "NCOLS must be in range");
        add_mar_activity(arows, ncols);
    }
    void add_mar_activity(unsigned mm, unsigned nn)
    {
        const auto ramp = mar_ramp(mm, nn);
        unsigned pos = 0;
        for (const auto activity : ramp) {
            if (pos < mar_activity.size()) {
                mar_activity[pos] += activity;
            } else if (pos == mar_activity.size()) {
                mar_activity.push_back(activity);
            } else {
                AMPERE_ERROR("Fell off end of MAR activity tracker");
            }
            ++pos;
        }
    }
    void pop_mar_activity_tracker()
    {
        if (mar_activity.size()) {
            mar_activity.pop_front();
        }
    }
    int supercell_count()
    {
        if (mar_activity.size()) {
            return mar_activity.front();
        }
        return 0;
    }
    sc::Event ev_matmul{"matmul", "Matmul execute"};
    sc::Event ev_movb{"movb", "Movb execute"};
    sc::Event ev_debug{"debug", "Debug execute"};
    sc::Event ev_syncwait{"syncwait", "SyncWait execute"};
    sc::Event ev_syncreaddone{"syncreaddone", "SyncReadDone execute"};
    sc::Event ev_syncwritedone{"syncwritedone", "SyncWriteDone execute"};
    sc::Event ev_loadimr_req{"loadimr_req", "LOADIMR request"};
    sc::Event ev_loadscale_req{"loadscale_req", "LOADSCALE request"};
    sc::Event ev_storeimr_req{"storeimr_req", "STOREIMR request"};
    sc::Event ev_storeamr_req{"storeamr_req", "STOREAMR request"};
    sc::EventData<addr_t> evd_addr{"addr", "Address"};
    sc::EventData<register_t> evd_mr{"mr", "matrix register"};
    sc::EventData<int> evd_supercell_count{"supercell_count", "Active supercell count"};

    std::multimap<sc::Cycle, aice_instruction_t> xu_pipeline;
    std::multimap<sc::Cycle, amr_write_sequence_t> amr_write_generator;
    std::vector<std::shared_ptr<tmem_fsm_t>> tmem_load_fsms;
    std::vector<std::shared_ptr<tmem_fsm_t>> tmem_store_fsms;
    unsigned tmem_fsm_instance_ctr{0};
    std::uint64_t mem_req_id{0};

    void done_execute(aice_instruction_t& inst)
    {
        DBG("ExecuteDone {} {:#x} {}", inst->inum, inst->pc, inst->disasm());
        out->put(inst);
    }
    void create_tmem_fsm_for_store_amr_vector(aice_instruction_t& inst)
    {
        ++tmem_fsm_instance_ctr;
        const unsigned nrows = metadata->amr_active_rows.at(inst->storeamr_src_amr());
        const tmem_fsm_direction_t dir = (inst->dtype == aice_emu::dtype_t::bf16)
                                             ? tmem_fsm_direction_t::AMR_SRC_VECTOR_BF16
                                             : tmem_fsm_direction_t::AMR_SRC_VECTOR;
        auto ptr = std::make_shared<tmem_fsm_t>(inst,
                                                dir,
                                                tmem_fsm_instance_ctr,
                                                inst->storeamr_addr(),
                                                inst->storeamr_src_amr(),
                                                nrows,
                                                inst->storeamr_stride());
        ptr->delay_until = now + store_delay;
        tmem_store_fsms.emplace_back(ptr);
    }
    void create_tmem_fsm_for_store_amr(aice_instruction_t& inst, const std::uint32_t naccumulators)
    {
        ++tmem_fsm_instance_ctr;
        const unsigned nrows = metadata->amr_active_rows.at(inst->storeamr_src_amr());
        const tmem_fsm_direction_t dir = (inst->dtype == aice_emu::dtype_t::bf16) ? tmem_fsm_direction_t::AMR_SRC_BF16
                                                                                  : tmem_fsm_direction_t::AMR_SRC;
        auto ptr = std::make_shared<tmem_fsm_t>(inst,
                                                dir,
                                                tmem_fsm_instance_ctr,
                                                inst->storeamr_addr(),
                                                inst->storeamr_src_amr(),
                                                nrows,
                                                inst->storeamr_stride(),
                                                naccumulators);
        ptr->delay_until = now + store_delay;
        tmem_store_fsms.emplace_back(ptr);
    }
    void create_tmem_fsm_for_store_imr(aice_instruction_t& inst)
    {
        ++tmem_fsm_instance_ctr;
        const unsigned nrows = metadata->imr_active_rows.at(inst->storeimr_src_imr());
        auto ptr = std::make_shared<tmem_fsm_t>(inst,
                                                tmem_fsm_direction_t::IMR_SRC,
                                                tmem_fsm_instance_ctr,
                                                inst->storeimr_addr(),
                                                inst->storeimr_src_imr(),
                                                nrows,
                                                inst->storeimr_stride());
        ptr->delay_until = now + store_delay; // FIXME might be different than storeamr
        tmem_store_fsms.emplace_back(ptr);
    }
    void create_tmem_fsm_for_load_scale(aice_instruction_t& inst, unsigned nscale)
    {
        unsigned transfers = (nscale * 64) / load_transfer_bytes;
        ++tmem_fsm_instance_ctr;
        auto ptr = std::make_shared<tmem_fsm_t>(inst,
                                                tmem_fsm_direction_t::IMR_SCALE_DEST,
                                                tmem_fsm_instance_ctr,
                                                inst->loadimr_addr(),
                                                inst->loadimr_dst_imr(),
                                                transfers,
                                                0);

        tmem_load_fsms.emplace_back(ptr);
    }

    void create_tmem_fsm_for_load(aice_instruction_t& inst)
    {
        ++tmem_fsm_instance_ctr;
        auto ptr = std::make_shared<tmem_fsm_t>(inst,
                                                tmem_fsm_direction_t::IMR_DEST,
                                                tmem_fsm_instance_ctr,
                                                inst->loadimr_addr(),
                                                inst->loadimr_dst_imr(),
                                                inst->loadimr_nrows(),
                                                inst->loadimr_stride());

        tmem_load_fsms.emplace_back(ptr);
    }

    void send_mr_done_to_decode(const tmem_fsm_direction_t direction, regidx_t mr, aice_instruction_t& inst)
    {
        switch (direction) {
        case tmem_fsm_direction_t::IMR_SRC:
            DBG("FSM: IMR READ DONE {}", mr);
            imr_read_done_out->put(wb_t{mr, inst});
            break;
        case tmem_fsm_direction_t::AMR_SRC:
        case tmem_fsm_direction_t::AMR_SRC_BF16:
        case tmem_fsm_direction_t::AMR_SRC_VECTOR:
        case tmem_fsm_direction_t::AMR_SRC_VECTOR_BF16:
            DBG("FSM: AMR READ DONE {}", mr);
            amr_read_done_out->put(wb_t{mr, inst});
            break;
        case tmem_fsm_direction_t::IMR_DEST:
            DBG("FSM: IMR WRITE DONE {}", mr);
            imr_write_done_out->put(wb_t{mr, inst});
            break;
        case tmem_fsm_direction_t::IMR_SCALE_DEST:
            DBG("FSM: IMR SCALE WRITE DONE {}", mr);
            imr_scale_write_done_out->put(wb_t{mr, inst});
            break;
        }
    }

    void tmem_fsms_step()
    {
        while (mem_resp_port->can_take()) { // process incoming loadimr responses from TMEM
            const auto& rsp = mem_resp_port->take();
            auto fsm = rsp->fsm;
            bool mr_done = fsm->tally_mem_response();
            DBG("TMEM FSM{} memory response received. Response counter {}",
                fsm->get_tmem_fsm_instance(),
                fsm->response_counter);
            if (mr_done) {
                send_mr_done_to_decode(fsm->get_direction(), rsp->mr, fsm->inst);
            }
        }

        tmem_fsms_step_kind(tmem_load_fsms);
        tmem_fsms_step_kind(tmem_store_fsms);
    }
    void tmem_fsms_step_kind(std::vector<std::shared_ptr<tmem_fsm_t>>& tmem_fsms)
    {
        for (auto fsm : tmem_fsms) {
            if (fsm->is_done_sending_requests()) {
                DBG("TMEM FSM{}: {} is done making requests, skip stepping",
                    fsm->get_tmem_fsm_instance(),
                    util::enum_to_string<tmem_fsm_direction_t>(fsm->get_direction()));
                continue;
            }
            if (fsm->delay_until > now) {
                continue;
            }
            const auto direction = fsm->get_direction();

            const addr_t addr = fsm->get_mem_ref();
            const regidx_t mr = fsm->get_reg_idx();
            // launch memory references
            switch (direction) {
            case tmem_fsm_direction_t::IMR_SRC:
                ev_storeimr_req(fsm->inst, evd_addr(addr), evd_mr(radd(register_t::imr0, mr)));
                fsm->step();
                DBG("STOREIMR {:#x} for AMR {}, requests_sent {}", addr, mr, fsm->get_requests_sent());
                mem_req_port->put(
                    make_child<mem_req_t>(fsm->get_instruction(), mem_req_id, access_type_t::STORE, addr, fsm, mr));
                ++mem_req_id;
                break;
            case tmem_fsm_direction_t::AMR_SRC:
            case tmem_fsm_direction_t::AMR_SRC_BF16:
            case tmem_fsm_direction_t::AMR_SRC_VECTOR:
            case tmem_fsm_direction_t::AMR_SRC_VECTOR_BF16:
                ev_storeamr_req(fsm->inst, evd_addr(addr), evd_mr(radd(register_t::amr0, mr)));
                fsm->step();
                DBG("STOREAMR {:#x} for AMR {}, requests_sent {}", addr, mr, fsm->get_requests_sent());
                mem_req_port->put(
                    make_child<mem_req_t>(fsm->get_instruction(), mem_req_id, access_type_t::STORE, addr, fsm, mr));
                ++mem_req_id;
                break;
            case tmem_fsm_direction_t::IMR_DEST: // LOADIMR, LOADMX, LOADMXU
                ev_loadimr_req(fsm->inst, evd_addr(addr), evd_mr(radd(register_t::imr0, mr)));
                fsm->step();
                DBG("LOADIMR {:#x} for IMR {}, requests_sent {}", addr, mr, fsm->get_requests_sent());
                mem_req_port->put(
                    make_child<mem_req_t>(fsm->get_instruction(), mem_req_id, access_type_t::LOAD, addr, fsm, mr));
                ++mem_req_id;
                break;
            case tmem_fsm_direction_t::IMR_SCALE_DEST: // LOADSCALE
                ev_loadscale_req(fsm->inst, evd_addr(addr), evd_mr(radd(register_t::scale0, mr)));
                fsm->step();
                DBG("LOADSCALE {:#x} for IMR {}, requests_sent {}", addr, mr, fsm->get_requests_sent());
                mem_req_port->put(
                    make_child<mem_req_t>(fsm->get_instruction(), mem_req_id, access_type_t::LOAD, addr, fsm, mr));
                ++mem_req_id;
                break;
            }
        }
        for (auto fsm : tmem_fsms) {
            fsm->status();
            // don't wait for all the responses to start new LOADIMR
            if (!fsm->is_written_back() && fsm->is_done_sending_requests()) {
                DBG("TMEM FSM{}: finishing execution", fsm->get_tmem_fsm_instance());
                done_execute(fsm->get_instruction());
                fsm->set_written_back(); // make sure we don't write back more than once while responses are still
                                         // coming in
            }
        }

        bool need_to_delete = false;
        for (auto fsm : tmem_fsms) {
            if (fsm->is_done()) {
                DBG("TMEM FSM{} DELETE", fsm->get_tmem_fsm_instance());
                need_to_delete = true;
            }
        }
        if (need_to_delete) {
            // remove the TMEM FSMs that are done. (Lovely C++!)
            tmem_fsms.erase(std::remove_if(tmem_fsms.begin(),
                                           tmem_fsms.end(),
                                           [](const std::shared_ptr<tmem_fsm_t> f) { return f->is_done(); }),
                            tmem_fsms.end());
        }
    }
};

class Commit : public sc::Module
{
protected:
    void end_of_simulation() override
    {
        Module::end_of_simulation();
        std::cout << std::format("FOR_J_USE: {} cycles {} instructions\n", get_cycle(), instr_retired);
    }

public:
    sc::Param<unsigned> max_instructions{"max_instructions", "Max instructions", 10 * 1000 * 1000};
    sc::Param<unsigned> heartbeat{"heartbeat", "Heatbeat", 1000};
    unsigned instr_retired{0};
    explicit Commit(const sc_core::sc_module_name& name) : sc::Module(name) {}

    sc::In<aice_instruction_t> in{"in"};

    void emit_commit_event(const const_aice_instruction_t& txn) { commit_event(txn); }

    bool run() override
    {
        while (in->can_take()) {
            aice_instruction_t txn = in->take();
            emit_commit_event(txn);
            commit_proc(txn);
            DBG("Commit: {} {:#x} {}", txn->inum, txn->pc, txn->disasm());
        }
        return false;
    }
    void commit_proc(aice_instruction_t& txn)
    {
        ++instr_retired;
        if (instr_retired >= max_instructions) {
            AMPERE_WARNING(
                "Maximum instruction count ({}) reached @ {}", max_instructions, sc_core::sc_time_stamp().to_string());
            sc_core::sc_stop();
        }
        if (instr_retired && instr_retired % heartbeat == 0) {
            print_heartbeat("Simulate", instr_retired);
        }
        if (txn->is_synchalt()) {
            AMPERE_WARNING("Ending. Committed synchalt. retired: {}, time: {}",
                           instr_retired,
                           sc_core::sc_time_stamp().to_string());
            sc_core::sc_stop();
        }
    }
    void print_heartbeat(const std::string& phase, std::uint64_t instrs)
    {
        static unsigned last_heartbeat_cycle{0};
        const sc::Cycle curr = get_cycle();
        std::cout << std::format("Heartbeat: {} {:>12} cycles, {:>11} instructions, IPC = {:.2f}\n",
                                 phase,
                                 curr,
                                 instrs,
                                 heartbeat / static_cast<float>(curr - last_heartbeat_cycle));
        std::cout << std::flush;
        last_heartbeat_cycle = curr;
    }

private:
    sc::Event commit_event{"commit", "Commit instruction"};
};

class Top : public sc::Module
{
public:
    Top(const sc_core::sc_module_name& name, sc::Clock* clock, sc::Environment* env);
    bool run() override
    {
        cycles_event();
        return true;
    }

private:
    MetaData metadata; // shared by decode & fetch by Top()
    Fetch fetch{"fetch"};
    Decode decode{"decode"}; // and writeback
    Execute execute{"execute"};
    Commit commit{"commit"};
    Memory memory{"memory"};
    sc::Event cycles_event{"cycles", "Cycles"};
};

} // namespace ampere::kestrel::aice
