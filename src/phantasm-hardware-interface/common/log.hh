#pragma once

#include <rich-log/detail/log_impl.hh>

namespace phi::detail
{
static constexpr rlog::domain domain = rlog::domain("PHI");

static constexpr rlog::domain vk_domain = rlog::domain("VULKAN");

inline void info_log(rlog::MessageBuilder& builder) { builder.set_domain(domain); }
inline void warn_log(rlog::MessageBuilder& builder)
{
    builder.set_domain(domain);
    builder.set_severity(rlog::severity::warning);
}
inline void trace_log(rlog::MessageBuilder& builder)
{
    builder.set_domain(domain);
    builder.set_severity(rlog::severity::trace);
}
inline void err_log(rlog::MessageBuilder& builder)
{
    builder.set_domain(domain);
    builder.set_severity(rlog::severity::error);
}
inline void assert_log(rlog::MessageBuilder& builder)
{
    builder.set_domain(domain);
    builder.set_severity(rlog::severity::critical);
}
inline void vulkan_log(rlog::MessageBuilder& builder)
{
    builder.set_domain(vk_domain);
    builder.set_severity(rlog::severity::warning);
}
} // namespace phi::detail

#define PHI_LOG RICH_LOG_IMPL(phi::detail::info_log)
#define PHI_LOG_WARN RICH_LOG_IMPL(phi::detail::warn_log)
#define PHI_LOG_TRACE RICH_LOG_IMPL(phi::detail::trace_log)
#define PHI_LOG_ERROR RICH_LOG_IMPL(phi::detail::err_log)
#define PHI_LOG_ASSERT RICH_LOG_IMPL(phi::detail::assert_log)
