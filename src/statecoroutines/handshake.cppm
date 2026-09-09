module;
#include <boost/asio.hpp>
#include <boost/intrusive_ptr.hpp>
#include <boost/smart_ptr/intrusive_ref_counter.hpp>
#include <print>
export module actualklasterkraft.statecoroutines.handshake;

import actualklasterkraft.packetops;
import actualklasterkraft.protocolprimitives;
import actualklasterkraft.formatters;
import actualklasterkraft.templates;
import actualklasterkraft.transport;
import actualklasterkraft.statecoroutines.status;
import actualklasterkraft.statecoroutines.login;

namespace asio = boost::asio;
using asio::ip::tcp;
using namespace protocolprimitives;

export namespace statecoroutines
{
    asio::awaitable<void> handshake(Transport transport)
    {
        auto fail = [&]
        {
            std::println(
                "Connection {} tried to connect but could not pass Handshake stage. Is it a Minecraft client?",
                transport.remote_endpoint_copy);
            transport.socket.shutdown(tcp::socket::shutdown_both);
            transport.socket.close();
        };

        std::array<uint8_t, 512> buf;
        auto it = buf.begin();

        auto [ec, packet_size]
            = co_await packetops::get(transport, asio::buffer(buf));
        if (ec)
            co_return fail();
        auto end = it + packet_size;

        auto packet_id = InlineTie(TieReturn, it, ec)
            = read_var<uint32_t>(it, end);
        if (ec || packet_id != 0x00)
            co_return fail();

        // Protocol Version (won't check it for now)
        std::tie(std::ignore, it, ec) = read_var<int32_t>(it, end);
        if (ec)
            co_return fail();

        // Server Address length
        auto server_addr_len = InlineTie(TieReturn, it, ec)
            = read_var<uint32_t>(it, end);
        if (ec || server_addr_len > 255)
            co_return fail();

        // Skip the following string and next field which is u16 Server Port
        it += server_addr_len + sizeof(uint16_t);
        if (it >= end)
            co_return fail();

        int32_t intent = InlineTie(TieReturn, it, ec)
            = read_var<uint32_t>(it, end);
        if (ec)
            co_return fail();

        asio::awaitable<void> next_coro { };
        if (intent == 1) // Status
            next_coro = statecoroutines::status(std::move(transport));
        else if (intent == 2 || intent == 3) // Login or Transfer
            next_coro
                = statecoroutines::login(std::move(transport), intent == 3);
        else
            co_return fail();

        if (it != end)
            co_return fail();

        asio::co_spawn(transport.socket.get_executor(), std::move(next_coro),
            asio::detached);
    }
}
