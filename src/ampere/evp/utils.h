//
// Copyright(c) 2022 Ampere Computing LLC
// SPDX-License-Identifier: BSD-3-Clause
//
#pragma once

#include <filesystem>
#include <regex>
#include <string>
#include <vector>

namespace ampere::evp {

namespace fs = std::filesystem;

std::string get_prog_absolute_path();
void set_config_path();
const char* get_config_path();
const char* get_pylib_path();

bool is_pattern(const std::string& s);
std::string glob_to_pattern(const std::string& glob);
std::string join_as_alts(const std::vector<std::string>& alts);
std::regex regex(const std::string& s);

bool is_time_spec(const std::string& s);
uint64_t parse_time_spec(const std::string& s);

static inline std::pair<std::string, std::string> extract_prefix(const std::string& name)
{
    auto prefix_index = name.find_last_of('.');
    return {prefix_index == std::string::npos ? "" : name.substr(0, prefix_index),
            prefix_index == std::string::npos ? name : name.substr(prefix_index + 1)};
}

template<typename T>
struct Singleton
{
    static T& instance()
    {
        T* ptr{nullptr};

        if (!ptr) {
            static T obj{};
            ptr = &obj;
        }

        return *ptr;
    }
};

} // namespace ampere::evp
