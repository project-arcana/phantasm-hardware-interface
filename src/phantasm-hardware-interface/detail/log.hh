#pragma once

#include <rich-log/log.hh>

namespace phi::log
{
static constexpr rlog::domain domain = rlog::domain("PHI");
static constexpr rlog::severity assert_severity = rlog::severity("ASSERT", "\u001b[38;5;196m\u001b[1m");

static constexpr rlog::domain vk_domain = rlog::domain("VULKAN");
static constexpr rlog::severity vk_severity = rlog::severity("LAYER", "\u001b[38;5;202m");

constexpr void phi_log(rlog::MessageBuilder& builder) { builder.set_domain(domain); }
constexpr void phi_log_warn(rlog::MessageBuilder& builder)
{
    builder.set_domain(domain);
    builder.set_severity(rlog::severity::warning());
    builder.set_use_error_stream(true);
}
constexpr void phi_log_err(rlog::MessageBuilder& builder)
{
    builder.set_domain(domain);
    builder.set_severity(rlog::severity::error());
    builder.set_use_error_stream(true);
}
constexpr void phi_log_assert(rlog::MessageBuilder& builder)
{
    builder.set_domain(domain);
    builder.set_severity(assert_severity);
    builder.set_use_error_stream(true);
}
constexpr void phi_log_vulkan(rlog::MessageBuilder& builder)
{
    builder.set_domain(vk_domain);
    builder.set_severity(vk_severity);
    builder.set_use_error_stream(true);
}
}

#define PHI_LOG RICH_LOG_IMPL(phi::log::phi_log)
#define PHI_LOG_WARN RICH_LOG_IMPL(phi::log::phi_log_warn)
#define PHI_LOG_ERROR RICH_LOG_IMPL(phi::log::phi_log_err)
#define PHI_LOG_ASSERT RICH_LOG_IMPL(phi::log::phi_log_assert)
