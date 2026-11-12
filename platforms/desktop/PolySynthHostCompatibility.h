#pragma once

#include <optional>

// Use the SDK's 64-bit optional sentinels when compiling the host on ARM64.
#if defined(__aarch64__) && !defined(__arm__)
#define TINY_OPTIONAL_x64
#endif

#ifndef _GLIBCXX_THROW_OR_ABORT
#define _GLIBCXX_THROW_OR_ABORT(exception) throw (exception)
#endif
