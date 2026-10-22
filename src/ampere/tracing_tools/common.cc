//
// Copyright (c) 2020-2022 Ampere Computing. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause
//

#include <stdio.h>
#include <stdarg.h>

extern "C" {
#include <qemu-plugin.h>
}
#include "common.h"

namespace ampere::tracer {

void log(const char* str, ...)
{
    const unsigned msg_len = 5 * 1024;
    char message[msg_len];
    va_list args;
    va_start(args, str);
    vsnprintf(message, msg_len - 1, str, args);

    qemu_plugin_outs(message);

    va_end(args);
}

#define QEMU_REG_READ(fnname, type)                                   \
    bool fnname(type* dest, int reg)                                  \
    {                                                                 \
        unsigned read_bytes;                                          \
        GByteArray* regval;                                           \
                                                                      \
        regval = g_byte_array_sized_new(sizeof(type));                \
        read_bytes = qemu_plugin_read_register(regval, reg);          \
        if (read_bytes == regval->len && regval->len == sizeof(type)) \
            *dest = *(uint64_t*)(regval->data);                       \
        else                                                          \
            read_bytes = 0;                                           \
        g_byte_array_free(regval, true);                              \
                                                                      \
        return read_bytes != 0;                                       \
    }

QEMU_REG_READ(qemu_plugin_read_reg64, uint64_t)
QEMU_REG_READ(qemu_plugin_read_reg32, uint32_t)

void read_pred_from_qemu(ampere::archx::preg_t& dest, unsigned regid, unsigned first_predicate_idx)
{
    const size_t nbytes = sizeof(ampere::archx::pred_reg_t);
    GByteArray* regval = g_byte_array_new();

    unsigned read_bytes = qemu_plugin_read_register(regval, regid + first_predicate_idx);
    if (read_bytes != regval->len || regval->len < nbytes) {
        g_byte_array_free(regval, true);
        THROW("Failed to read SVE predicate reg {}", regid);
    }

    for (size_t i = 0; i < nbytes; i++)
        dest.u8[i] = regval->data[i];
    g_byte_array_free(regval, true);
}

void read_zreg_from_qemu(ampere::archx::vector_t& dest, unsigned regid, unsigned first_zreg_idx)
{
    // ignores VL > 128b
    GByteArray* regval = g_byte_array_new();

    unsigned read_bytes = qemu_plugin_read_register(regval, regid + first_zreg_idx);
    if (read_bytes != regval->len || regval->len < ampere::archx::MAXVL) {
        g_byte_array_free(regval, true);
        THROW("Failed to read SVE Z-reg {}", regid);
    }

    for (size_t i = 0; i < ampere::archx::MAXVL; i++)
        dest.u8[i] = regval->data[i];
    g_byte_array_free(regval, true);
}

} // namespace ampere::tracer

/*
 * vim: sw=4:ts=4:et:si:cindent:cino=\:0,(0,g0,t0:
 */
