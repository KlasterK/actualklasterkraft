#ifndef ACTKK_ERRC_HPP
#define ACTKK_ERRC_HPP

#include <boost/system.hpp>
#include <string>

enum class MCProtocolError
{
    VarIntTooBig,
    UnexpectedPacketID,
    ExcessPacketData,
    UnsufficientPacketData,
};

class MCProtocolErrorCategory : public boost::system::error_category
{
private:
    MCProtocolErrorCategory() = default;

public:
    static MCProtocolErrorCategory &instance();

    const char *name() const noexcept override;
    std::string message(int value) const override;
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

inline boost::system::error_code make_error_code(MCProtocolError err)
{
    return { static_cast<int>(err), MCProtocolErrorCategory::instance() };
}

#endif // ACTKK_ERRC_HPP
