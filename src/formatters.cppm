module;
#include <boost/asio.hpp>
#include <boost/system.hpp>
#include <format>
#include <spanstream>
export module actualklasterkraft.formatters;

template <> struct std::formatter<boost::system::error_code, char>
{
    constexpr std::format_parse_context::iterator parse(
        std::format_parse_context &ctx) const
    {
        if (ctx.begin() != ctx.end() && *ctx.begin() != '}')
            throw std::format_error(
                "std::formatter<boost::system::error_code, char>::parse: error_code formating doesn't support flags");
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
                "std::formatter<boost::asio::ip::tcp::endpoint, char>::parse: endpoint formating doesn't support flags");
        return ctx.begin();
    }

    std::format_context::iterator format(
        boost::asio::ip::tcp::endpoint endpoint, std::format_context &ctx) const
    {
        std::array<char, 64> buf;
        std::ospanstream oss { buf };
        oss << endpoint;
        return std::copy(oss.span().begin(), oss.span().end(), ctx.out());
    }
};
