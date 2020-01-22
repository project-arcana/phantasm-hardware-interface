#pragma once

#include <rich-log/MessageBuilder.hh>

namespace phi::vk::log
{
inline rlog::MessageBuilder info()
{
    //
    return rlog::MessageBuilder(rlog::prefix("[phi][vk] "));
}

inline rlog::MessageBuilder err()
{
    //
    return rlog::MessageBuilder(rlog::prefix("[phi][vk] "), rlog::err_out);
}

}
