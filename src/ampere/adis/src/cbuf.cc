//
// Copyright (c) 2022 Ampere Computing. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause
//


#include "cbuf.h"
#include <assert.h>
#include <string.h>

namespace adis {
void adis_cbuf_init(cbuf_t* c) {
    c->len = 0;
    c->buf[0] = 0;
}
void adis_cbuf_append(cbuf_t* c, char* cstring) {
    uint32_t nlen = strlen(cstring);
    char* p = cstring;
    assert(c);
    assert(c->len + nlen < ADIS_CBUF_LEN);

    while (*p) {
        c->buf[c->len++] = *p++;
    }
    c->buf[c->len] = 0;
}

void adis_cbuf_append_char(cbuf_t* c, char b) {
    assert(c);
    assert(c->len+2 < ADIS_CBUF_LEN);
    c->buf[c->len++] = b;
    c->buf[c->len] = 0;
}
}
