#pragma once

#include <rich-log/MessageBuilder.hh>

namespace phi::d3d12::log
{
static constexpr rlog::domain domain = rlog::domain("PHI");
static constexpr rlog::severity dred_severity = rlog::severity("DRED", "\u001b[38;5;196m\u001b[1m");

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

inline rlog::MessageBuilder dred()
{
    //
    return rlog::MessageBuilder(dred_severity, domain, rlog::sep(""), rlog::err_out);
}

}
