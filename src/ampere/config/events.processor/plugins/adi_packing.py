# Copyright (c) 2024-2025 Ampere Computing. All rights reserved.
# SPDX-License-Identifier: BSD-3-Clause

import evp

outfile = open("adi_packing.csv", "w")
print("link,reqs,snps,dhdr,rsp0,rsp1,dats", file=outfile)


def record_adi_packing(event):
    link = event.name[:-5]
    reqs = event.data.get("pack_reqs", 0)
    snps = event.data.get("pack_snps", 0)
    dhdr = event.data.get("pack_dathdrs", 0)
    rsp0 = event.data.get("pack_rsp0", 0)
    rsp1 = event.data.get("pack_rsp1", 0)
    dats = event.data.get("pack_dats", 0)
    print(f"{event.time},{link},{reqs},{snps},{dhdr},{rsp0},{rsp1},{dats}", file=outfile)


evp.on("*.pack", record_adi_packing)
evp.end_simulation(lambda: outfile.close())
