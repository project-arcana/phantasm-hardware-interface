#pragma once

#include <cstddef>
#include <cstdio>

#include <clean-core/span.hh>

namespace phi
{
// formats an amount of bytes into a human readable version
inline int byte_print(size_t numBytes, cc::span<char> outString)
{
    // idea from https://codegolf.stackexchange.com/a/52202
    double l = double(numBytes);
    char const* u = " KMGTPEZY";
    while ((l /= 1024.) >= .95 && *u)
        ++u;
    return snprintf(outString.data(), outString.size(), *u - ' ' ? "%.1f %ciB" : "%.0f B", l * 1024.0, *u);
}

// formats an amount of things into a SI-prefixed version
inline int si_print(size_t amount, cc::span<char> outString)
{
    double l = double(amount);
    char const* u = " kMGTPEZY";
    while ((l /= 1000) >= .95 && *u)
        ++u;
    return snprintf(outString.data(), outString.size(), *u - ' ' ? "%.1f%c" : "%.0f", l * 1024.0, *u);
}

} // namespace phi