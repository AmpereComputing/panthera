#!/usr/bin/env python3
# Copyright (c) 2023-2026 Ampere Computing. All rights reserved.
# SPDX-License-Identifier: BSD-3-Clause

from __future__ import annotations

import re


def latex_fix(t: str):
    s = str(t)
    s = re.sub(r"{", "{{", s)
    s = re.sub(r"}", "}}", s)
    s = re.sub(r"_", "\\_", s)
    s = re.sub(r"<", "$<$", s)
    s = re.sub(r">", "$>$", s)
    s = re.sub(r"/", "/ ", s)
    return s


def longtable(rows: list[list[str]], header: list[str], caption: str, maxcolwidths: list[str] = None) -> str:
    """This uses the longtable package, so \\usepackage{longtable} in
    your document header. Do NOT wrap this longtable in figure (or
    other similar) environment or it will not be able to do page
    breaks for long tables.

    maxcolwidths is an optional list of column widths. An element can
    be 'None' if you don't care about the width; the dafault is
    'l'. An element can be an l,r,c for left, right, center
    alignemnt. Or it can be a latex width measurement like '2in' to be
    used in a 'p' parbox. If supplied, maxcolwidths is required to
    have a value for each column."""
    s = []  # the string output list, joined to a string at return
    pattern_lst = []
    ncol = len(header)
    if maxcolwidths:
        for width in maxcolwidths:
            if width:
                if width in ["l", "c", "r"]:
                    code = width
                else:
                    code = f"p{{{width}}}"
            else:
                code = "l"
            pattern_lst.append(code)
    else:
        pattern_lst = ["l"] * ncol
    pattern = "|" + "|".join(pattern_lst) + "|"
    latex_hdr = " & ".join(header) + "\\\\"

    s.append("\\begin{longtable}{" + pattern + "}")
    s.append("\\caption{" + caption + "} \\\\")
    # first header
    s.append("\\hline")
    s.append(latex_hdr)
    s.append("\\hline")
    s.append("\\endfirsthead")

    # continuation header(s)
    s.append("\\hline")
    s.append("\\multicolumn{" + str(ncol) + "}{|r|}{{\\tablename\\ \\thetable{} -- Continued from previous page}} \\\\")
    s.append("\\hline")
    s.append(latex_hdr)
    s.append("\\hline")
    s.append("\\endhead")

    # continuation footer(s)
    s.append("\\hline")
    s.append("\\multicolumn{" + str(ncol) + "}{|r|}{{Continued on next page}} \\\\")
    s.append("\\hline")
    s.append("\\endfoot")

    # final footer
    s.append("\\hline")
    s.append("\\endlastfoot")

    for row in rows:
        prow = [latex_fix(x) for x in row]
        s.append(" & ".join(prow) + "\\\\")
        s.append("\\hline")
    s.append("\\end{longtable}")
    return "\n".join(s)
