module;
#include <boost/system.hpp>
#include <string>
export module actualklasterkraft.errc;

export enum class MCProtocolError {
    VarIntTooBig,
    UnexpectedPacketID,
    ExcessPacketData,
    UnsufficientPacketData,
    NotEnoughBuffersToFitPacket,
    MalformedPacketHeader,
    CorrelationIDMismatch,
};

export class MCProtocolErrorCategory : public boost::system::error_category
{
private:
    MCProtocolErrorCategory() = default;

public:
    static MCProtocolErrorCategory &instance()
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
        default:
            return std::format("Unknown 0x{:x}", value, value);
        }
    }
};

namespace boost
{
    namespace system
    {
        template <> struct is_error_code_enum<MCProtocolError> : std::true_type
        {
        };
    }
}

export boost::system::error_code make_error_code(MCProtocolError err)
{
    return { static_cast<int>(err), MCProtocolErrorCategory::instance() };
}
