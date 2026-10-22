//
// Copyright (c) 2022 Ampere Computing. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause
//

#include <algorithm>
#include <iostream>
#include <sstream>
#include <string>

#include "ampere/branch_stream/branch_stream.h"

using namespace std::literals;
using namespace ampere::bs;

struct Options
{
    long wait_branches{0};
    long capture_branches{50000000};
};

template<typename T>
void get_option(T& opt_val, const std::string& argument)
{
    std::istringstream iss{argument};
    iss >> opt_val;
}

void print_help_and_exit()
{
    std::cerr << "Usage: bs_chop [options] <input file> <output file>\n";
    std::cerr << "Options:\n";
    std::cerr << "  --wait      Number of branches to wait (skip) before starting\n";
    std::cerr << "  --capture   Number of branches to include\n";

    exit(1);
}

std::vector<std::string> parse_arguments(std::vector<std::string> arguments, Options& options)
{
    std::vector<std::string> positional_arguments;

    try {
        for (size_t i = 0; i < arguments.size(); i++) {
            if ("--wait"s == arguments[i])
                get_option(options.wait_branches, arguments.at(++i));
            else if ("--capture"s == arguments[i])
                get_option(options.capture_branches, arguments.at(++i));
            else if ("--help"s == arguments[i] || "-h"s == arguments[i])
                print_help_and_exit();
            else if (arguments[i][0] == '-')
                throw std::runtime_error{"unrecognized option: "s + arguments[i]};
            else
                positional_arguments.push_back(arguments[i]);
        }
    } catch (std::out_of_range&) {
        std::cerr << "missing argument value" << std::endl;
        print_help_and_exit();
    } catch (std::runtime_error& err) {
        std::cerr << err.what() << std::endl;
        print_help_and_exit();
    }

    return positional_arguments;
}

int main(int argc, char** argv)
{
    Options options;

    auto files = parse_arguments(std::vector<std::string>(argv + 1, argv + argc), options);

    if (files.size() != 2)
        print_help_and_exit();

    boost::filesystem::path input_file_path(files[0]);
    boost::filesystem::path output_file_path(files[1]);

    BranchStreamReader* reader = new BranchStreamReader(input_file_path);
    BranchStreamWriter* writer = new BranchStreamWriter(output_file_path);
    Event event;

    std::cout << "Chopping " << input_file_path << " to " << options.capture_branches
              << " branches starting after seeing " << options.wait_branches << " branches." << std::endl;

    long branch_count = 0;

    /* Write all non-branch events, or any events after `wait_branches`
     * branches are seen. */
    while (reader->read(event)) {
        if (event.has_branch())
            branch_count++;

        if (branch_count > options.wait_branches + options.capture_branches) {
            break;
        } else if (!event.has_branch() || (branch_count > options.wait_branches)) {
            writer->write(event);
        }
        event.Clear();
    }
    writer->flush();

    if (branch_count < options.wait_branches + options.capture_branches) {
        std::cerr << "Warning: Insufficient branches in the input stream; wrote only "
                  << std::max(0L, branch_count - options.wait_branches) << " branches." << std::endl;
        return EXIT_FAILURE;
    }

    return EXIT_SUCCESS;
}

/*
 * vim: sw=4:ts=4:et:si:cindent:cino=\:0,(0,g0,t0:
 */
