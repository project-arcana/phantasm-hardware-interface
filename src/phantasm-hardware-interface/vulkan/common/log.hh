#pragma once

#include <rich-log/MessageBuilder.hh>

namespace pr::backend::vk::log
{
inline rlog::MessageBuilder info()
{
    //
    return rlog::MessageBuilder(rlog::prefix("[pr][backend][vk] "));
}

inline rlog::MessageBuilder err()
{
    //
    return rlog::MessageBuilder(rlog::prefix("[pr][backend][vk] "), rlog::err_out);
}

}
