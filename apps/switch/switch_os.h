// Switch specifics (os_switch.cpp): libnx services, libcurl downloads, sdmc folders, the shared font.
#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "vnr/rt/platform.h"

namespace vnr::nx {

void init_services();      // sockets (curl), ssl, pl (shared font); call first
void exit_services();      // in reverse; call last
std::string user_dir();    // sdmc:/switch/vnreader, created if missing
std::string cache_dir();   // sdmc:/switch/vnreader/cache, created if missing
std::vector<std::uint8_t> japanese_font();  // the system's Standard (Japanese) shared font
std::unique_ptr<rt::Http> make_http(int worker_threads = 4);

}  // namespace vnr::nx
