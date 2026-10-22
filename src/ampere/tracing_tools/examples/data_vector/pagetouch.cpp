//
// Copyright (c) 2024 Ampere Computing. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause
//

#include "tracing_hints.h"
#include <iostream>
#include <vector>

int main(int argc, char* argv[]) {
    if (argc < 3 || argc > 5) {
        std::cout << "Usage: " << argv[0] << " <number_of_pages> <number_of_times> [-r] [-w]" << std::endl;
        return 1;
    }

    int num_pages = std::stoi(argv[1]);
    int num_times = std::stoi(argv[2]);
    bool do_reads = false;
    bool do_writes = false;

    // Parse the command-line options
    for (int i = 3; i < argc; i++) {
        if (std::string(argv[i]) == "-r") {
            do_writes = true;
        } else if (std::string(argv[i]) == "-w") {
            do_reads = true;
        }
    }

    std::vector<char*> pages;

    for (int i = 0; i < num_pages; i++) {
        char* page = new char[4096];
        pages.push_back(page);
    }

    hint_start_tracing();
    for (int i = 0; i < num_times; i++) {
        for (int j = 0; j < num_pages; j++) {
            // offset into the page to do read/write, currently 0
            for (int k = 0; k < 4096; k += 4096) {
                if (do_writes) {
                    // writes
                    pages[j][k] = 0;
                }
                if (do_reads) {
                    // reads
                    volatile char value = pages[j][k];
                }
            }
        }
    }
    hint_stop_tracing();

    for (int i = 0; i < num_pages; i++) {
        delete[] pages[i];
    }

    std::cout << "SUCCESS" << std::endl;
    return 0;
}