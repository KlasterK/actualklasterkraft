
module;
#include <boost/asio.hpp>
#include <boost/intrusive_ptr.hpp>
#include <boost/smart_ptr/intrusive_ref_counter.hpp>
#include <boost/system.hpp>
#include <print>
export module actualklasterkraft.statecoroutines.status;

import actualklasterkraft.errc;
import actualklasterkraft.formatters;
import actualklasterkraft.packetops;
import actualklasterkraft.session;
import actualklasterkraft.streambufops;

namespace asio = boost::asio;
namespace sys = boost::system;

export namespace statecoroutines
{
    asio::awaitable<void> status(boost::intrusive_ptr<Session> session)
    {
        sys::error_code ec { };
        std::println("\tstate_status");

        auto fail = [&session](sys::error_code a_ec)
        {
            std::println(
                "\tProtocol desynced. Is it a Minecraft client? (error code: {})",
                a_ec);
            session->get_socket().shutdown(asio::ip::tcp::socket::shutdown_both);
            session->get_socket().close();
        };

        // Should get packet Status Request
        ec = co_await packetops::await_for_packet(*session);
        if (ec)
            co_return fail(ec);

        if (session->get_streambuf().sbumpc() != 0x00)
            co_return fail(MCProtocolError::UnexpectedPacketID);

        if (session->get_streambuf().size() > 0)
            co_return fail(MCProtocolError::ExcessPacketData);

        static constexpr std::string_view ExampleResponse = R"({
            "version": {
                "name": "26.1.2",
                "protocol": 775
            },
            "players": {
                "max": 20,
                "online": 1,
                "sample": []
            },
            "description": {
                "text": "Hello World!"
            },
            "enforcesSecureChat": false
        })";

        // send Status Response
        session->get_streambuf().sputc(0x00); // id
        streambufops::write_string(session->get_streambuf(), ExampleResponse);

        ec = co_await packetops::flush_packet(*session);
        if (ec)
            co_return fail(ec);

        // Should get packet Ping Request
        ec = co_await packetops::await_for_packet(*session);
        if (ec)
            co_return fail(ec);

        if (session->get_streambuf().sbumpc() != 0x01)
            co_return fail(MCProtocolError::UnexpectedPacketID);

        auto payload = streambufops::read_integer<uint64_t>(
            session->get_streambuf(), ec);
        if (ec)
            co_return fail(ec);

        if (session->get_streambuf().size() > 0)
            co_return fail(MCProtocolError::ExcessPacketData);

        // Pong Response
        session->get_streambuf().sputc(0x01); // id
        streambufops::write_integer<uint64_t>(
            session->get_streambuf(), payload);

        ec = co_await packetops::flush_packet(*session);
        if (ec)
            co_return fail(ec);

            
        session->get_socket().shutdown(asio::ip::tcp::socket::shutdown_both);
        session->get_socket().close();
    }
}
