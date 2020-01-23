#pragma once

#include <rich-log/MessageBuilder.hh>

namespace phi::d3d12::log
{
inline rlog::MessageBuilder info()
{
    //
    return rlog::MessageBuilder(rlog::prefix("[phi][d3d12] "), rlog::sep(""));
}

inline rlog::MessageBuilder err()
{
    //
    return rlog::MessageBuilder(rlog::prefix("[phi][d3d12] "), rlog::sep(""), rlog::err_out);
}

inline rlog::MessageBuilder dred()
{
    //
    return rlog::MessageBuilder(rlog::prefix("[phi][d3d12][DRED] "), rlog::sep(""), rlog::err_out);
}

}
