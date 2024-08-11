#include <zaplog.hpp>

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

} // namespace zaplog
