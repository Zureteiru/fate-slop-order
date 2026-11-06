// What differs between desktop OSes. Windows is implemented in os_windows.cpp; macOS/Linux get
// their own file later (libcurl HTTP, ~/Library/Caches or $XDG_CACHE_HOME, Hiragino / Noto CJK).
#pragma once

#include <memory>
#include <string>
#include <vector>

#include "vnr/rt/platform.h"

namespace vnr::desktop {

std::string cache_dir();                         // created if missing
std::string user_dir();                          // files the user supplies (ui/ sheets); created if missing
std::vector<std::string> japanese_font_paths();  // tried in order
std::unique_ptr<rt::Http> make_http(int worker_threads = 4);

}  // namespace vnr::desktop
