module;
#include <array>
#include <boost/asio.hpp>
#include <boost/system.hpp>
#include <string>
module actualklasterkraft.net.base.statecoroutines;

import actualklasterkraft.generic.completiontokens;
import actualklasterkraft.generic.errc;
import actualklasterkraft.generic.templates;
import actualklasterkraft.data.prebuiltconfiguration;
import actualklasterkraft.data.protocolprimitives;
import actualklasterkraft.world.player;
import actualklasterkraft.net.base.disconnecthelpers;
import actualklasterkraft.net.base.packetops;
import actualklasterkraft.net.base.transport;

namespace asio = boost::asio;
using asio::ip::tcp;
using namespace protocolprimitives;

asio::awaitable<void> statecoroutines::configuration(
    Transport transport, Player::SpawnInfo collected_info)
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
    auto ec = co_await packetops::put(transport, asio::buffer(&packet_id, 1));
    if (ec)
        co_return co_await disconnect::configuration(
            transport, disconnect::fmt_reason(ec, "Finish Configuration"));

    // Ignore serverbound configuration packets, wait for Acknowledge Finish Configuration
    for (std::array<uint8_t, 65536> buf;;)
    {
        auto [ec, packet_size]
            = co_await packetops::get(transport, asio::buffer(buf));
        if (ec)
            co_return co_await disconnect::configuration(transport,
                disconnect::fmt_reason(ec, "Acknowledge Finish Configuration"));
        if (packet_size < 1)
            co_return co_await disconnect::login(transport,
                disconnect::fmt_reason(MCProtocolError::UnsufficientPacketData,
                    "Acknowledge Finish Configuration"));

        // Client Information (used for determining wanted view distance)
        // TODO: implement this packet for Play stage as well
        if (buf[0] == 0x00)
        {
            auto it = buf.begin() + 1;
            auto end = buf.begin() + packet_size;

            auto locale_len = InlineTie(TieReturn, it, ec)
                = read_var<uint32_t>(it, end);
            if (ec)
                co_return co_await disconnect::configuration(transport,
                    disconnect::fmt_reason(ec, "Client Information"));
            it += locale_len;
            if (it >= end)
                co_return co_await disconnect::configuration(transport,
                    disconnect::fmt_reason(
                        MCProtocolError::UnsufficientPacketData,
                        "Client Information"));

            collected_info.view_distance = std::clamp<uint8_t>(*it++, 2, 32);
            // The remaining part of the packet isn't used, won't check it
            continue;
        }

        // Acknowledge Finish Configuration
        if (buf[0] == 0x03)
        {
            if (packet_size > 1) // No fields
                co_return co_await disconnect::configuration(transport,
                    disconnect::fmt_reason(MCProtocolError::ExcessPacketData,
                        "Acknowledge Finish Configuration"));
            break;
        }
    }

    // NOTE: grab the executor BEFORE moving transport (same use-after-move
    // as in login.cpp).
    auto executor = transport.socket.get_executor();
    asio::co_spawn(executor,
        statecoroutines::play(std::move(transport), std::move(collected_info)),
        detached_log_exceptions_token);
}
