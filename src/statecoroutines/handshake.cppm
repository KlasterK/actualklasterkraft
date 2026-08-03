module;
#include <boost/asio.hpp>
#include <boost/intrusive_ptr.hpp>
#include <boost/smart_ptr/intrusive_ref_counter.hpp>
#include <print>
export module actualklasterkraft.statecoroutines.handshake;

import actualklasterkraft.packetops;
import actualklasterkraft.session;
import actualklasterkraft.statecoroutines.status;
import actualklasterkraft.statecoroutines.login;
import actualklasterkraft.streambufops;

namespace asio = boost::asio;
namespace sys = boost::system;

export namespace statecoroutines
{
    asio::awaitable<void> handshake(boost::intrusive_ptr<Session> session)
    {
        sys::error_code ec { };
        std::println("\tstate_handshake");

        auto fail = [&]
        {
            std::println("\tProtocol desynced. Is it a Minecraft client?");
            session->get_socket().shutdown(
                asio::ip::tcp::socket::shutdown_both);
            session->get_socket().close();
        };

        if (co_await packetops::await_for_packet(*session))
            co_return fail();

        // Packet ID
        if (session->get_streambuf().sbumpc() != 0x00)
            co_return fail();

        // Protocol Version (won't check it for now)
        int32_t proto_version
            = streambufops::read_v32(session->get_streambuf(), ec);
        if (ec)
            co_return fail();
        std::println("\tProtocol Version: {}", proto_version);

        // Server Address length
        int32_t server_addr_len
            = streambufops::read_v32(session->get_streambuf(), ec);
        if (ec || server_addr_len < 1 || server_addr_len > 255)
            co_return fail();

        // Skip the following string and next field which is u16 Server Port
        session->get_streambuf().consume(server_addr_len + 2);

        int32_t intent = streambufops::read_v32(session->get_streambuf(), ec);
        if (ec)
            co_return fail();

        asio::awaitable<void> next_coro { };
        if (intent == 1) // Status
            next_coro = statecoroutines::status(session);
        else if (intent == 2 || intent == 3) // Login or Transfer
            next_coro = statecoroutines::login(session, intent == 3);
        else
            co_return fail();

        if (session->get_streambuf().size() > 0)
            co_return fail();

        asio::co_spawn(session->get_io(), std::move(next_coro),
            [session](std::exception_ptr exc_ptr)
            { session->handle_coroutine_finished(exc_ptr); });
    }
}
