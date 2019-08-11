#pragma once
#include <time.h>
#include <string>
#include <vector>

std::vector<std::string> getPathComponents(std::string_view root, time_t s, uint64_t sourceid);
std::string getPath(std::string_view root, time_t s, uint64_t sourceid, bool create=false);
