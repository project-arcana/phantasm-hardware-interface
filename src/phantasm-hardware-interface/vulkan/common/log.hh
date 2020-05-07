#pragma once

#include <rich-log/MessageBuilder.hh>

namespace phi::vk::log
{
static constexpr rlog::domain domain = rlog::domain("PHI");

inline rlog::MessageBuilder info()
{
    //
    return rlog::MessageBuilder(rlog::severity::info(), domain, rlog::sep(""));
}

inline rlog::MessageBuilder err()
{
    //
    return rlog::MessageBuilder(rlog::severity::error(), domain, rlog::sep(""), rlog::err_out);
}

}
