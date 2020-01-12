#pragma once
#include <time.h>
#include <string>
#include <vector>
#include "navmon.pb.h"

std::vector<std::string> getPathComponents(std::string_view root, time_t s, uint64_t sourceid);
std::string getPath(std::string_view root, time_t s, uint64_t sourceid, bool create=false);
bool getNMM(int fd, NavMonMessage& nmm, uint32_t& offset);
bool getNMM(FILE* fp, NavMonMessage& nmm, uint32_t& offset);
bool getRawNMM(FILE* fp, timespec& t, std::string& raw, uint32_t& offset);
