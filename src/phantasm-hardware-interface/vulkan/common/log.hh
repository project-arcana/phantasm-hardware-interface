#pragma once

#include <rich-log/MessageBuilder.hh>

namespace phi::vk::log
{
inline rlog::MessageBuilder info()
{
    //
    return rlog::MessageBuilder(rlog::prefix("[phi][vk] "), rlog::sep(""));
}

inline rlog::MessageBuilder err()
{
    //
    return rlog::MessageBuilder(rlog::prefix("[phi][vk] "), rlog::sep(""), rlog::err_out);
}

}
