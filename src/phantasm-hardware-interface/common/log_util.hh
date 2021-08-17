#pragma once

#include <phantasm-hardware-interface/common/api.hh>

namespace phi::log
{
PHI_API void dump_hex(void const* addr, int len);
}
