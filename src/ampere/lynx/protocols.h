/*
 * Copyright (c) 2024 Ampere Computing. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 */

#pragma once

#include "lynx.enum.h"
#include "types.h"

namespace ampere::lynx::protocols {

template<Opcode opcode,
         typename T,
         typename Agent,
         bool has_dmt_allowed = std::is_same_v<decltype(&T::dmt_allowed), bool>>
void Read_L3Miss(T t, Agent core)
{
    static_assert(opcode == Opcode::ReadShared || opcode == Opcode::ReadUnique);

    t.send(opcode, core, ComponentType::PxHA, [&](T t, Agent pxha) {
        t.send(opcode, pxha, ComponentType::PmHA, [&](T t, Agent pmha) {
            if (t.dmt_allowed(core, pmha)) {
                t.send(Opcode::ReadNoSnpSep, pmha, ComponentType::MCU, [&](T t, Agent mcu) {
                    t.send(Opcode::ReadReceipt, mcu, pmha);
                    t.send(Opcode::DataSepResp, mcu, core);
                });
                t.send(Opcode::RespSepData, pmha, pxha);
                t.send(Opcode::CompAck, pxha, pmha);
                t.send(Opcode::RespSepData, pxha, core);
            } else {
                t.send(Opcode::ReadNoSnp, pmha, ComponentType::MCU, [&](T t, Agent mcu) {
                    t.send(Opcode::CompData, mcu, pmha);
                });
                t.send(Opcode::CompData, pmha, pxha);
                t.send(Opcode::CompAck, pxha, pmha);
                t.send(Opcode::CompData, pxha, core);
            }
        });
        t.send(Opcode::CompAck, core, pxha);
    });
}

template<Opcode opcode, typename T, typename Agent>
void Read_L3Miss_DMT(T t, Agent core)
{
    static_assert(opcode == Opcode::ReadShared || opcode == Opcode::ReadUnique);

    t.send(opcode, core, ComponentType::PxHA, [&](T t, Agent pxha) {
        t.send(opcode, pxha, ComponentType::PmHA, [&](T t, Agent pmha) {
            t.send(Opcode::ReadNoSnpSep, pmha, ComponentType::MCU, [&](T t, Agent mcu) {
                t.send(Opcode::ReadReceipt, mcu, pmha);
                t.send(Opcode::DataSepResp, mcu, core);
            });
            t.send(Opcode::RespSepData, pmha, pxha);
            t.send(Opcode::CompAck, pxha, pmha);
        });
        t.send(Opcode::RespSepData, pxha, core);
        t.send(Opcode::CompAck, core, pxha);
    });
}

template<Opcode opcode, typename T, typename Agent>
void Read_L3Miss_NoDMT(T t, Agent core)
{
    static_assert(opcode == Opcode::ReadShared || opcode == Opcode::ReadUnique);

    t.send(opcode, core, ComponentType::PxHA, [&](T t, Agent pxha) {
        t.send(opcode, pxha, ComponentType::PmHA, [&](T t, Agent pmha) {
            t.send(Opcode::ReadNoSnp, pmha, ComponentType::MCU, [&](T t, Agent mcu) {
                t.send(Opcode::CompData, mcu, pmha);
            });
            t.send(Opcode::CompData, pmha, pxha);
            t.send(Opcode::CompAck, pxha, pmha);
        });
        t.send(Opcode::CompData, pxha, core);
        t.send(Opcode::CompAck, core, pxha);
    });
}

template<Opcode opcode, typename T, typename Agent>
void Read_CXL(T t, Agent core, Agent cxl)
{
    static_assert(opcode == Opcode::ReadShared || opcode == Opcode::ReadUnique);

    t.send(opcode, core, ComponentType::PxHA, [&](T t, Agent pxha) {
        t.send(opcode, pxha, ComponentType::PmHA, [&](T t, Agent pmha) {
            t.send(Opcode::ReadNoSnp, pmha, cxl, [&](T t, Agent cxl) { t.send(Opcode::CompData, cxl, pmha); });
            t.send(Opcode::CompData, pmha, pxha);
            t.send(Opcode::CompAck, pxha, pmha);
        });
        t.send(Opcode::CompData, pxha, core);
        t.send(Opcode::CompAck, core, pxha);
    });
}

template<Opcode opcode, typename T, typename Agent>
void Read_L3Hit(T t, Agent core)
{
    static_assert(opcode == Opcode::ReadShared || opcode == Opcode::ReadUnique);

    t.send(opcode, core, ComponentType::PxHA, [&](T t, Agent pxha) {
        constexpr Opcode snp_opcode = opcode == Opcode::ReadShared ? Opcode::SnpSharedFwd : Opcode::SnpUniqueFwd;
        t.send(snp_opcode, pxha, ComponentType::Core, [&](T t, Agent stash_core) {
            t.send(Opcode::SnpRespFwd, stash_core, pxha);
            t.send(Opcode::CompData, stash_core, core);
        });
        t.send(Opcode::CompAck, core, pxha);
    });
}

template<typename T, typename Agent>
void ReadShared_L3Miss(T t, Agent core)
{
    Read_L3Miss<Opcode::ReadShared>(t, core);
}

template<typename T, typename Agent>
void ReadShared_L3Miss_DMT(T t, Agent core)
{
    Read_L3Miss_DMT<Opcode::ReadShared>(t, core);
}

template<typename T, typename Agent>
void ReadShared_L3Miss_NoDMT(T t, Agent core)
{
    Read_L3Miss_NoDMT<Opcode::ReadShared>(t, core);
}

template<typename T, typename Agent>
void ReadShared_CXL(T t, Agent core, Agent cxl)
{
    Read_CXL<Opcode::ReadShared>(t, core, cxl);
}

template<typename T, typename Agent>
void ReadShared_L3Hit(T t, Agent core)
{
    Read_L3Hit<Opcode::ReadShared>(t, core);
}

template<typename T, typename Agent>
void ReadUnique_L3Miss(T t, Agent core)
{
    Read_L3Miss<Opcode::ReadUnique>(t, core);
}

template<typename T, typename Agent>
void ReadUnique_L3Miss_DMT(T t, Agent core)
{
    Read_L3Miss_DMT<Opcode::ReadUnique>(t, core);
}

template<typename T, typename Agent>
void ReadUnique_CXL(T t, Agent core, Agent cxl)
{
    Read_CXL<Opcode::ReadUnique>(t, core, cxl);
}

template<typename T, typename Agent>
void ReadUnique_L3Miss_NoDMT(T t, Agent core)
{
    Read_L3Miss_NoDMT<Opcode::ReadUnique>(t, core);
}

template<typename T, typename Agent>
void ReadUnique_L3Hit(T t, Agent core)
{
    Read_L3Hit<Opcode::ReadUnique>(t, core);
}

template<typename T, typename Agent>
void MakeUnique_L3Miss(T t, Agent core)
{
    t.send(Opcode::MakeUnique, core, ComponentType::PxHA, [&](T t, Agent pxha) {
        t.send(Opcode::MakeUnique, pxha, ComponentType::PmHA, [&](T t, Agent pmha) {
            t.send(Opcode::Comp, pmha, pxha);
            t.send(Opcode::CompAck, pxha, pmha);
        });
        t.send(Opcode::Comp, pxha, core);
        t.send(Opcode::CompAck, core, pxha);
    });
}

template<typename T, typename Agent>
void MakeUnique_L3Hit(T t, Agent core)
{
    t.send(Opcode::MakeUnique, core, ComponentType::PxHA, [&](T t, Agent pxha) {
        t.send(Opcode::SnpUnique, pxha, ComponentType::Core, [&](T t, Agent core) {
            t.send(Opcode::SnpResp, core, pxha);
        });
        t.send(Opcode::Comp, pxha, core);
        t.send(Opcode::CompAck, core, pxha);
    });
}

template<typename T, typename Agent>
void WriteBackFull(T t, Agent core)
{
    t.send(Opcode::WriteBackFull, core, ComponentType::PxHA, [&](T t, Agent pxha) {
        t.send(Opcode::SnpStashShared, pxha, ComponentType::Core, [&](T t, Agent stash_core) {
            t.send(Opcode::SnpResp, stash_core, pxha);
            t.send(Opcode::CompDBID, pxha, core);
            t.send(Opcode::CopyBackWrData, core, stash_core);
            t.send(Opcode::CompAck, stash_core, pxha);
        });
    });
}

template<typename T, typename Agent>
void WriteBackFull_FromL3(T t, Agent core)
{
    t.send(Opcode::WriteBackFull, core, ComponentType::PxHA, [&](T t, Agent pxha) {
        t.send(Opcode::CompDBID, pxha, core);
        t.send(Opcode::CopyBackWrData, core, pxha);
        t.send(Opcode::WriteBackFull, pxha, ComponentType::PmHA, [&](T t, Agent pmha) {
            t.send(Opcode::CompDBID, pmha, pxha);
            t.send(Opcode::CopyBackWrData, pxha, pmha);
            t.send(Opcode::WriteNoSnpFull, pmha, ComponentType::MCU, [&](T t, Agent mcu) {
                t.send(Opcode::CompDBID, mcu, pmha);
                t.send(Opcode::CopyBackWrData, pmha, mcu);
            });
        });
    });
}

template<typename T, typename Agent>
void WriteEvictFull(T t, Agent core)
{
    t.send(Opcode::WriteEvictFull, core, ComponentType::PxHA, [&](T t, Agent pxha) {
        t.send(Opcode::SnpStashShared, pxha, ComponentType::Core, [&](T t, Agent stash_core) {
            t.send(Opcode::SnpResp, stash_core, pxha);
            t.send(Opcode::CompDBID, pxha, core);
            t.send(Opcode::CopyBackWrData, core, stash_core);
            t.send(Opcode::CompAck, stash_core, pxha);
        });
    });
}

template<typename T, typename Agent>
void Evict(T t, Agent core)
{
    t.send(Opcode::Evict, core, ComponentType::PxHA, [&](T t, Agent pxha) {
        t.send(Opcode::Comp, pxha, core);
        t.send(Opcode::CompAck, core, pxha);
        t.send(Opcode::Evict, pxha, ComponentType::PmHA, [&](T t, Agent pmha) {
            t.send(Opcode::Comp, pmha, pxha);
            t.send(Opcode::CompAck, pxha, pmha);
        });
    });
}

/** ReadOnce as generated by IORs. In some circumstances an IOR can generate
 *  a ReadNoSnp as well, but we don't currently model that.
 */
template<typename T, typename Agent>
void ReadOnce(T t, Agent ior)
{
    t.send(Opcode::ReadOnce, ior, ComponentType::PmHA, [&](T t, Agent pmha) {
        t.send(Opcode::ReadNoSnp, pmha, ComponentType::MCU, [&](T t, Agent mcu) {
            t.send(Opcode::ReadReceipt, mcu, pmha);
            t.send(Opcode::CompData, mcu, pmha);
            t.send(Opcode::CompData, pmha, ior);
        });
        // t.send(Opcode::CompAck, ior, pmha); Optional
    });
}

template<typename T, typename Agent>
void WriteNoSnpPtl(T t, Agent ior)
{
    t.send(Opcode::WriteNoSnpPtl, ior, ComponentType::PmHA, [&](T t, Agent pmha) {
        t.send(Opcode::CompDBID, pmha, ior);
        t.send(Opcode::NonCopyBackWrData, ior, pmha);

        t.send(Opcode::WriteNoSnpPtl, pmha, ComponentType::MCU, [&](T t, Agent mcu) {
            t.send(Opcode::CompDBID, mcu, pmha);
            t.send(Opcode::NonCopyBackWrData, pmha, mcu);
        });

        t.send(Opcode::CompAck, ior, pmha);
    });
}

template<typename T, typename Agent>
void ReadNoSnp_P2P(T t, Agent ior, Agent ioh)
{
    t.send(Opcode::ReadNoSnp, ior, ioh, [&](T t, Agent ioh) {
        t.send(Opcode::ReadReceipt, ioh, ior);
        t.send(Opcode::CompData, ioh, ior);
        t.send(Opcode::CompData, ioh, ior);
        t.send(Opcode::CompData, ioh, ior);
        t.send(Opcode::CompData, ioh, ior);
    });
}

template<typename T, typename Agent>
void WriteNoSnp_P2P(T t, Agent ior, Agent ioh)
{
    t.send(Opcode::WriteNoSnpPtl, ior, ioh, [&](T t, Agent ioh) {
        t.send(Opcode::DBIDResp, ioh, ior);
        t.send(Opcode::NonCopyBackWrData, ior, ioh);
        t.send(Opcode::NonCopyBackWrData, ior, ioh);
        t.send(Opcode::NonCopyBackWrData, ior, ioh);
        t.send(Opcode::NonCopyBackWrData, ior, ioh);
        t.send(Opcode::Comp, ioh, ior);
    });
}

} // namespace ampere::lynx::protocols
