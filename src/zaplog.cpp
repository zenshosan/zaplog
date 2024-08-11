#include <git_date.h>
#include <git_hash.h>
#include <zaplog.hpp>
#include <zaplog_config.h>

namespace zaplog {

Zaplog::Zaplog()
{
    number = 6;
}

int
Zaplog::get_number() const
{
    return number;
}

std::string
Zaplog::get_version() const
{
    return ZAPLOG_VERSION;
}
std::string
Zaplog::get_git_hash() const
{
    return ZAPLOG_GIT_HASH;
}

std::string
Zaplog::get_git_date() const
{
    return ZAPLOG_GIT_DATE;
}
} // namespace zaplog
