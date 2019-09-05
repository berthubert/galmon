#pragma once
#include <stdint.h>
#include <unistd.h>
struct EofException{};
size_t readn2(int fd, void* buffer, size_t len);
