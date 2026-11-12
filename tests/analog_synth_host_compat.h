#pragma once

#include <optional>

// The SDK's bundled optional recognizes 32-bit ARM and x86. The host synth
// tests use its integer and float sentinels on 64-bit ARM as well.
#if defined(__aarch64__) && !defined(__arm__)
#define TINY_OPTIONAL_x64
#endif

#ifndef _GLIBCXX_THROW_OR_ABORT
#define _GLIBCXX_THROW_OR_ABORT(exception) throw (exception)
#endif
