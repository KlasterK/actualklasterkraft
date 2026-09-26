module;
#include <array>
#include <boost/asio.hpp>
#include <boost/system.hpp>
#include <string>
export module actualklasterkraft.statecoroutines.configuration;

import actualklasterkraft.disconnecthelpers;
import actualklasterkraft.errc;
import actualklasterkraft.packetops;
import actualklasterkraft.prebuiltconfiguration;
import actualklasterkraft.transport;
import actualklasterkraft.statecoroutines.play;

namespace asio = boost::asio;
using asio::ip::tcp;

export namespace statecoroutines
{
    asio::awaitable<void> configuration(Transport transport,
        std::string player_name, std::array<uint8_t, 16> player_uuid)
    {
        // For Configuration, we should synchronise our game data with client's game data.
        // We'll ignore serverbound packets for simplicity.

        auto packet_it = PrebuiltConfigurationStagePackets.data.data();
        for (size_t packet_length : PrebuiltConfigurationStagePackets.lengths)
        {
            if (packet_length == 0)
                break;

            auto ec = co_await packetops::put(
                transport, asio::buffer(packet_it, packet_length));
            if (ec)
                co_return co_await disconnect::configuration(transport,
                    disconnect::fmt_reason(
                        ec, "prebuilt Configuration stage packets"));
            packet_it += packet_length;
        }

        // Finish Configuration (no fields)
        uint8_t packet_id = 0x03;
        auto ec
            = co_await packetops::put(transport, asio::buffer(&packet_id, 1));
        if (ec)
            co_return co_await disconnect::configuration(
                transport, disconnect::fmt_reason(ec, "Finish Configuration"));

        // Ignore any packets until Acknowledge Finish Configuration
        for (std::array<uint8_t, 65536> buf;;)
        {
            auto [ec, packet_size]
                = co_await packetops::get(transport, asio::buffer(buf));
            if (ec)
                co_return co_await disconnect::configuration(transport,
                    disconnect::fmt_reason(
                        ec, "Acknowledge Finish Configuration"));
            if (packet_size < 0)
                co_return co_await disconnect::login(transport,
                    disconnect::fmt_reason(
                        MCProtocolError::UnsufficientPacketData,
                        "Acknowledge Finish Configuration"));

            // Acknowledge Finish Configuration
            if (buf[0] == 0x03)
            {
                if (packet_size > 1) // No fields
                    co_return co_await disconnect::login(transport,
                        disconnect::fmt_reason(
                            MCProtocolError::ExcessPacketData,
                            "Acknowledge Finish Configuration"));
                break;
            }
        }

        asio::co_spawn(transport.socket.get_executor(),
            statecoroutines::play(std::move(transport), std::move(player_name),
                std::move(player_uuid)),
            detached_rethrow_token);
    }
}
