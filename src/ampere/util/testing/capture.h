//
// Copyright (c) 2025 Ampere Computing. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause
//

#include <ostream>
#include <sstream>
#include <iostream>

class CaptureStream
{
    std::ostream &os;
    std::stringstream captured;
    decltype(os.rdbuf()) previous{nullptr};

public:
    explicit CaptureStream(std::ostream &os) : os(os), previous(os.rdbuf()) { os.rdbuf(captured.rdbuf()); }
    ~CaptureStream() { os.rdbuf(previous); }
    auto operator*() { return captured.str(); }
};

class CaptureStdout : public CaptureStream
{
public:
    CaptureStdout() : CaptureStream(std::cout) {}
};

class CaptureStderr : public CaptureStream
{
public:
    CaptureStderr() : CaptureStream(std::cerr) {}
};
