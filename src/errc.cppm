module;
#include <boost/system.hpp>
#include <string>
export module actualklasterkraft.errc;

export enum class MCProtocolError {
    VarIntTooBig,
    UnexpectedPacketID,
    ExcessPacketData,
    UnsufficientPacketData,
    CorrelationIDMismatch,
    BufferTooSmallForPacket,
};

export enum class MCGameError {
    EntityWasKilled,
    ServerClosed,
};

export class MCProtocolErrorCategory : public boost::system::error_category
{
private:
    MCProtocolErrorCategory() = default;

public:
    static auto &instance()
    {
        static MCProtocolErrorCategory category;
        return category;
    }

    const char *name() const noexcept override
    {
        return "MCProtocolErrorCategory";
    }

    std::string message(int value) const override
    {
        switch (static_cast<MCProtocolError>(value))
        {
        case MCProtocolError::VarIntTooBig:
            return "VarInt is too big";
        case MCProtocolError::UnexpectedPacketID:
            return "Unexpected packet ID";
        case MCProtocolError::ExcessPacketData:
            return "Excess packet data";
        case MCProtocolError::UnsufficientPacketData:
            return "Unsufficient packet data";
        case MCProtocolError::CorrelationIDMismatch:
            return "Correlation ID mismatch";
        case MCProtocolError::BufferTooSmallForPacket:
            return "Buffer is too small for the packet";
        default:
            return std::format("Unknown 0x{:02X}", value);
        }
    }
};

export class MCGameErrorCategory : public boost::system::error_category
{
private:
    MCGameErrorCategory() = default;

public:
    static auto &instance()
    {
        static MCGameErrorCategory category;
        return category;
    }

    const char *name() const noexcept override { return "MCGameErrorCategory"; }

    std::string message(int value) const override
    {
        switch (static_cast<MCGameError>(value))
        {
        case MCGameError::EntityWasKilled:
            return "Entity was killed";
        case MCGameError::ServerClosed:
            return "Server closed";
        default:
            return std::format("Unknown 0x{:02X}", value);
        }
    }
};

namespace boost::system
{
    template <> struct is_error_code_enum<MCProtocolError> : std::true_type
    {
    };

    template <> struct is_error_code_enum<MCGameError> : std::true_type
    {
    };
}

export boost::system::error_code make_error_code(MCProtocolError err)
{
    return { static_cast<int>(err), MCProtocolErrorCategory::instance() };
}

export boost::system::error_code make_error_code(MCGameError err)
{
    return { static_cast<int>(err), MCGameErrorCategory::instance() };
}