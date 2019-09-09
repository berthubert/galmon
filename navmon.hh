#pragma once
#include <stdint.h>
#include <unistd.h>
#include <time.h>
#include <string>

struct EofException{};
size_t readn2(int fd, void* buffer, size_t len);
std::string humanTime(time_t t);
