//
// Copyright (c) 2022 Ampere Computing. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause
//

# pragma once
#include <stdint.h>
namespace adis {
#define ADIS_CBUF_LEN 80
typedef struct {
    // null terminated so buffer usable conventionally.
    uint32_t len;
    char buf[ADIS_CBUF_LEN];
} cbuf_t;


void adis_cbuf_init(cbuf_t* c);
void adis_cbuf_append(cbuf_t* c, char* cstring);
void adis_cbuf_append_char(cbuf_t* c, char b);
}
