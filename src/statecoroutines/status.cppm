module;
#include <boost/asio.hpp>
#include <boost/container/small_vector.hpp>
#include <boost/json.hpp>
#include <boost/system.hpp>
#include <print>
#include <ranges>
export module actualklasterkraft.statecoroutines.status;

import actualklasterkraft.errc;
import actualklasterkraft.formatters;
import actualklasterkraft.packetops;
import actualklasterkraft.protocolprimitives;
import actualklasterkraft.templates;
import actualklasterkraft.transport;
import actualklasterkraft.world.player;

namespace asio = boost::asio;
namespace sys = boost::system;
using asio::ip::tcp;
using namespace protocolprimitives;

std::string generate_status_response_json()
{
    return boost::json::serialize(boost::json::value {
        { "version",
            {
                { "name", "26.1.2" },
                { "protocol", 775 },
            } },
        { "players",
            {
                { "max", get_global_player_pool().max_players() },
                { "online", get_global_player_pool().count_taken_slots() },
                { "sample",
                    get_global_player_pool().living_players()
                        | std::views::take(20)
                        | std::views::transform(
                            [](Player &p)
                            {
                                return boost::json::value { { "name",
                                                                p.get_name() },
                                    { "id",
                                        std::format("{}",
                                            FormatAsUUID { p.get_uuid() }) } };
                            })
                        | std::ranges::to<boost::json::array>() },
            } },
        { "description",
            {
                { "text", "An ActualKlasterKraft Server" },
            } },
        { "enforcesSecureChat", false },
    });
}

export namespace statecoroutines
{
    asio::awaitable<void> status(Transport transport)
    {
        auto fail = [&](sys::error_code a_ec)
        {
            std::println(
                "Connection {} requested Server List Ping but an error occured: {}",
                transport.remote_endpoint_copy, a_ec);
            transport.socket.shutdown(tcp::socket::shutdown_both);
            transport.socket.close();
        };

        std::array<uint8_t, 16> buf;

        auto [ec, packet_size]
            = co_await packetops::get(transport, asio::buffer(buf));
        if (ec)
            co_return fail(ec);

        if (packet_size < 1)
            co_return fail(MCProtocolError::UnsufficientPacketData);
        if (packet_size > 1)
            co_return fail(MCProtocolError::ExcessPacketData);
        if (buf[0] != 0x00) // Status Request
            co_return fail(MCProtocolError::UnexpectedPacketID);

        // Status Response packet ID matches
        {
            std::string json = generate_status_response_json();
            auto end = write_var<uint32_t>(buf.data() + 1, json.size());
            ec = co_await packetops::put_va(transport,
                asio::buffer(buf.data(), end - buf.data()), asio::buffer(json));
            if (ec)
                co_return fail(ec);
        }

        std::tie(ec, packet_size)
            = co_await packetops::get(transport, asio::buffer(buf));
        if (ec)
            co_return fail(ec);

        if (packet_size < 9)
            co_return fail(MCProtocolError::UnsufficientPacketData);
        if (packet_size > 9)
            co_return fail(MCProtocolError::ExcessPacketData);
        if (buf[0] != 0x01) // Ping Request
            co_return fail(MCProtocolError::UnexpectedPacketID);

        // Pong Response looks the same as Pong Request so we send it back without changes
        ec = co_await packetops::put(transport, asio::buffer(buf.data(), 9));
        if (ec)
            co_return fail(ec);

        std::println("Connection {} requested Server List Ping",
            transport.remote_endpoint_copy);
        transport.socket.shutdown(tcp::socket::shutdown_both);
        transport.socket.close();
    }
}
