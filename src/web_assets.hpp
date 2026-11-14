// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 github@nwdxlgzs
#pragma once
#include <string>
namespace hls {
std::string embedded_page(bool gzip);
struct EmbeddedIcon {std::string body;const char* content_type;const char* etag;};
EmbeddedIcon embedded_icon(const std::string& path,bool gzip);
struct EmbeddedPreview {std::string body,content_type,etag;};
EmbeddedPreview embedded_preview(const std::string& path,bool gzip);
}
