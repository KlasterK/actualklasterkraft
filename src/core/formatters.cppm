module;
#include <algorithm>
#include <boost/asio.hpp>
#include <boost/system.hpp>
#include <format>
#include <span>
export module actualklasterkraft.formatters;

template <> struct std::formatter<boost::system::error_code, char>
{
    constexpr std::format_parse_context::iterator parse(
        std::format_parse_context &ctx) const
    {
        if (ctx.begin() != ctx.end() && *ctx.begin() != '}')
            throw std::format_error(
                "std::formatter<boost::system::error_code, char>::parse: error_code formatting doesn't support flags");
        return ctx.begin();
    }

    std::format_context::iterator format(
        boost::system::error_code ec, std::format_context &ctx) const
    {
        std::string msg = ec.what();
        return std::copy(msg.begin(), msg.end(), ctx.out());
    }
};

template <> struct std::formatter<boost::asio::ip::tcp::endpoint, char>
{
    constexpr std::format_parse_context::iterator parse(
        std::format_parse_context &ctx) const
    {
        if (ctx.begin() != ctx.end() && *ctx.begin() != '}')
            throw std::format_error(
                "std::formatter<boost::asio::ip::tcp::endpoint, char>::parse: endpoint formatting doesn't support flags");
        return ctx.begin();
    }

    std::format_context::iterator format(
        boost::asio::ip::tcp::endpoint endpoint, std::format_context &ctx) const
    {
        std::ostringstream oss;
        oss << endpoint;
        std::string str = std::move(oss).str();
        return std::copy(str.begin(), str.end(), ctx.out());
    }
};

export struct FormatAsUUID
{
    std::span<const uint8_t, 16> data;
};

template <> struct std::formatter<FormatAsUUID, char>
{
    constexpr std::format_parse_context::iterator parse(
        std::format_parse_context &ctx) const
    {
        if (ctx.begin() != ctx.end() && *ctx.begin() != '}')
            throw std::format_error(
                "std::formatter<FormatAsUUID, char>::parse: endpoint formatting doesn't support flags");
        return ctx.begin();
    }

    std::format_context::iterator format(
        FormatAsUUID uuid, std::format_context &ctx) const
    {
        return std::format_to(ctx.out(),
            "{:02x}{:02x}{:02x}{:02x}-{:02x}{:02x}-{:02x}{:02x}-{:02x}{:02x}-{:02x}{:02x}{:02x}{:02x}{:02x}{:02x}",
            uuid.data[0], uuid.data[1], uuid.data[2], uuid.data[3],
            uuid.data[4], uuid.data[5], uuid.data[6], uuid.data[7],
            uuid.data[8], uuid.data[9], uuid.data[10], uuid.data[11],
            uuid.data[12], uuid.data[13], uuid.data[14], uuid.data[15]);
    }
};
