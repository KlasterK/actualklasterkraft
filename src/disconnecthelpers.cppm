module;
#include <boost/asio.hpp>
#include <boost/intrusive_ptr.hpp>
#include <boost/smart_ptr/intrusive_ref_counter.hpp>
#include <boost/system.hpp>
#include <print>
#include <string_view>
export module actualklasterkraft.disconnecthelpers;

import actualklasterkraft.session;
import actualklasterkraft.streambufops;
import actualklasterkraft.packetops;
import actualklasterkraft.formatters;
import actualklasterkraft.errc;
import actualklasterkraft.nbtbuilder;

using namespace nbtbuilderdefinitions;

boost::asio::awaitable<void> epilog(Session &session, std::string_view reason)
{
    auto ec = co_await packetops::flush_packet(session);
    if (ec)
        std::println(
            "\tCan't send disconnect message to the client (error code: {}; reason: {})",
            ec, reason);
    else
        std::println("\tClient was disconnected with reason: {}", reason);

    session.get_socket().shutdown(boost::asio::ip::tcp::socket::shutdown_both);
    session.get_socket().close();
}

export namespace disconnect
{
    auto login(Session &session, std::string_view reason)
    {
        constexpr std::string_view json_left = "{\"text\":\"",
                                   json_right = "\"}";

        session.get_streambuf().consume(session.get_streambuf().size());

        session.get_streambuf().sputc(0x00);
        streambufops::write_v32(session.get_streambuf(),
            json_left.size() + reason.size() + json_right.size());

        session.get_streambuf().sputn(json_left.data(), json_left.size());
        for (char c : reason)
        {
            if (c == '"')
                session.get_streambuf().sputn("\\\"", 2);
            else if (c == '\\')
                session.get_streambuf().sputn("\\\\", 2);
            else
                session.get_streambuf().sputc(c);
        }
        session.get_streambuf().sputn(json_right.data(), json_right.size());

        return epilog(session, reason);
    }

    auto configuration(Session &session, std::string_view reason)
    {
        session.get_streambuf().consume(session.get_streambuf().size());

        session.get_streambuf().sputc(0x02);
        NBTBuilder(std::ostreambuf_iterator(&session.get_streambuf()))
            << String << reason;

        return epilog(session, reason);
    }

    auto play(Session &session, std::string_view reason)
    {
        session.get_streambuf().consume(session.get_streambuf().size());

        session.get_streambuf().sputc(0x20);
        NBTBuilder(std::ostreambuf_iterator(&session.get_streambuf()))
            << String << reason;

        return epilog(session, reason);
    }

    std::string fmt_desync(
        boost::system::error_code ec, std::string_view opt_ctx)
    {
        return opt_ctx.empty()
            ? std::format("Protocol Desync : {}", ec)
            : std::format("Protocol Desync ({}) : {}", opt_ctx, ec);
    }
}
