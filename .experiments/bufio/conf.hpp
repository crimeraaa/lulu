#pragma once

#include <cstddef>  // size_t
#include <cstdio>   // EOF
#include <cstring>  // strlen
#include <cstdlib>  // realloc, free

#include <stdexcept>
#include <memory>

#define cast                static_cast
#define cast_int(expr)      cast<int>(expr)
#define unused(expr)        cast<void>(expr)
#define unused2(x, y)       unused(x), unused(y)
#define unused3(x, y, z)    unused2(x, y), unused(z)

using std::size_t;

using Number = double;

struct Global;

struct Slice {
    const char *string;
    size_t      length;
};

static constexpr size_t MAX_INPUT      = 512;
static constexpr size_t MIN_BUFFER     = 32;
static constexpr size_t MAX_TO_CSTRING = 64;
