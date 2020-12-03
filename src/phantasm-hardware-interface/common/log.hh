#pragma once

#include <rich-log/detail/log_impl.hh>

namespace phi::detail
{
static constexpr rlog::domain domain = rlog::domain("PHI");
static constexpr rlog::severity assert_severity = rlog::severity("ASSERT", "\u001b[38;5;196m\u001b[1m");

static constexpr rlog::domain vk_domain = rlog::domain("VULKAN");
static constexpr rlog::severity vk_severity = rlog::severity("LAYER", "\u001b[38;5;202m");

inline void info_log(rlog::MessageBuilder& builder) { builder.set_domain(domain); }
inline void warn_log(rlog::MessageBuilder& builder)
{
    builder.set_domain(domain);
    builder.set_severity(rlog::severity::warning());
    builder.set_use_error_stream(true);
}
inline void trace_log(rlog::MessageBuilder& builder)
{
    builder.set_domain(domain);
    builder.set_severity(rlog::severity::trace());
}
inline void err_log(rlog::MessageBuilder& builder)
{
    builder.set_domain(domain);
    builder.set_severity(rlog::severity::error());
    builder.set_use_error_stream(true);
}
inline void assert_log(rlog::MessageBuilder& builder)
{
    builder.set_domain(domain);
    builder.set_severity(assert_severity);
    builder.set_use_error_stream(true);
}
inline void vulkan_log(rlog::MessageBuilder& builder)
{
    builder.set_domain(vk_domain);
    builder.set_severity(vk_severity);
    builder.set_use_error_stream(true);
}
}

#define PHI_LOG RICH_LOG_IMPL(phi::detail::info_log)
#define PHI_LOG_WARN RICH_LOG_IMPL(phi::detail::warn_log)
#define PHI_LOG_TRACE RICH_LOG_IMPL(phi::detail::trace_log)
#define PHI_LOG_ERROR RICH_LOG_IMPL(phi::detail::err_log)
#define PHI_LOG_ASSERT RICH_LOG_IMPL(phi::detail::assert_log)
