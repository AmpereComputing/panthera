//
// Copyright(c) 2022 Ampere Computing LLC
// SPDX-License-Identifier: BSD-3-Clause
//
#include <fmt/format.h>
#include <unistd.h>
#include <cstdlib>
#include <regex>
#include <sstream>

#include "utils.h"

namespace ampere::evp {

namespace {

std::string config_path{};
std::string pylib_path{};

const std::regex is_pattern_re{"[*?#\\[\\]!]"};
const std::regex time_spec_re("([0-9]+)(([munp])?s)?");

void replace_all(std::string& s, const std::string& search, const std::string& replace)
{
    size_t pos = s.find(search);

    while (pos != std::string::npos) {
        s.replace(pos, search.size(), replace);
        pos = s.find(search, pos + replace.size());
    }
}

} // namespace

std::string get_prog_absolute_path()
{
    char result[PATH_MAX];
    ssize_t count = readlink("/proc/self/exe", result, PATH_MAX);

    if (count != -1) {
        result[count] = '\0';
        return std::string{result};
    } else {
        throw std::runtime_error{fmt::format("Failed to get the evp program's absolute path")};
    }
}

/** Compute our likely config directory from the installation path of
 *  the program. That is:
 *
 *    <path>/install/bin/evp ==> <path>/install/config/events.processor
 *
 *  While we're at it, we also set the installed path to Python libraries:
 *
 *    <path>/install/lib/python
 */
void set_config_path()
{
    fs::path absprog = get_prog_absolute_path();
    auto install_path = absprog.parent_path().parent_path();
    pylib_path = (install_path / "lib" / "python").native();

    auto* config_path_env = getenv("EVP_CONFIG_PATH");
    if (config_path_env)
        config_path = fs::path(config_path_env).native();
    else
        config_path = (install_path / "config" / "events.processor").native();
}

const char* get_config_path()
{
    return config_path.c_str();
}

const char* get_pylib_path()
{
    return pylib_path.c_str();
}

bool is_pattern(const std::string& s)
{
    std::smatch m;
    return std::regex_search(s, m, is_pattern_re);
}

std::string glob_to_pattern(const std::string& glob)
{
    std::string pattern{glob};
    replace_all(pattern, ".", "\\.");
    replace_all(pattern, "*", "[A-Za-z0-9_.]*");
    replace_all(pattern, "?", "[A-Za-z0-9_]+");
    replace_all(pattern, "#", "[0-9]+");
    replace_all(pattern, "!", "^");
    replace_all(pattern, "(^", "(?!");
    return pattern;
}

std::string join_as_alts(const std::vector<std::string>& alts)
{
    std::stringstream ss;
    bool first{true};

    for (const auto& alt : alts) {
        if (!first)
            ss << '|';
        else
            first = false;
        ss << '(' << alt << ')';
    }

    return ss.str();
}

std::regex regex(const std::string& s)
{
    try {
        return std::regex{s};
    } catch (std::regex_error& err) {
        throw std::runtime_error{fmt::format("error in regex: {}, expression was {}", err.what(), s)};
    }
}

bool is_time_spec(const std::string& s)
{
    std::smatch m;
    return std::regex_match(s, m, time_spec_re);
}

uint64_t parse_time_spec(const std::string& s)
{
    std::smatch m;

    if (std::regex_match(s, m, time_spec_re)) {
        uint64_t v = std::stoull(m[1]);

        if (m.size() > 1) {
            switch (m[2].str()[0]) {
            case 's':
                v *= 1000000000000;
                break;
            case 'm':
                v *= 1000000000;
                break;
            case 'u':
                v *= 1000000;
                break;
            case 'n':
                v *= 1000;
                break;
            case 'p':
                v *= 1;
                break;
            }
        }

        return v;
    }

    throw std::runtime_error{fmt::format("illegal time specification \"{}\"", s)};
}

} // namespace ampere::evp
