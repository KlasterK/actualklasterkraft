#include <errc.hpp>

MCProtocolErrorCategory &MCProtocolErrorCategory::instance()
{
    static MCProtocolErrorCategory category;
    return category;
}

const char *MCProtocolErrorCategory::name() const noexcept
{
    return "MCJEProtocolErrorCategory";
}

std::string MCProtocolErrorCategory::message(int value) const
{
    switch (static_cast<MCProtocolError>(value))
    {
    case MCProtocolError::VarIntTooBig:
        return "VarInt is too big";
    default:
        return "Unknown";
    }
}
