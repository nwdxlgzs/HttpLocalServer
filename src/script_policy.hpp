// SPDX-License-Identifier: Apache-2.0
#pragma once
#include <string>
namespace hls {
bool valid_ip_rule(const std::string &rule);
bool matches_ip_rule(const std::string &ip, const std::string &rule);
} // namespace hls
