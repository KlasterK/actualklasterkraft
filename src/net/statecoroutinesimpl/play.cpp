module;
#include <boost/asio.hpp>
#include <boost/asio/experimental/awaitable_operators.hpp>
#include <boost/asio/experimental/channel.hpp>
#include <boost/container/static_vector.hpp>
#include <boost/system.hpp>
#include <chrono>
#include <cstdint>
#include <new>
#include <print>
#include <random>
module actualklasterkraft.net.base.statecoroutines;

import actualklasterkraft.generic.completiontokens;
import actualklasterkraft.generic.errc;
import actualklasterkraft.generic.formatters;
import actualklasterkraft.generic.math;
import actualklasterkraft.generic.pubsub;
import actualklasterkraft.generic.templates;
import actualklasterkraft.data.bitfields;
import actualklasterkraft.data.nbtbuilder;
import actualklasterkraft.data.protocolprimitives;
import actualklasterkraft.world.chunk;
import actualklasterkraft.world.player;
import actualklasterkraft.net.base.disconnecthelpers;
import actualklasterkraft.net.base.packetops;
import actualklasterkraft.net.base.transport;
import actualklasterkraft.net.play.chat;
import actualklasterkraft.net.play.chunkserialization;
import actualklasterkraft.net.play.entityplacement;
import actualklasterkraft.net.play.finalisation;
import actualklasterkraft.net.play.initiation;
import actualklasterkraft.net.play.keepalive;
import actualklasterkraft.net.play.motion;
import actualklasterkraft.net.play.playerlist;

namespace asio = boost::asio;
namespace sys = boost::system;
using namespace protocolprimitives;
using namespace asio::experimental::awaitable_operators;
using namespace std::literals;
using asio::experimental::channel;
using asio::ip::tcp;

constexpr uint32_t ServerboundClientTickEndPacketID = 0x0D;

static std::minstd_rand g_rng { std::random_device { }() };

asio::awaitable<void> statecoroutines::play(
    Transport transport, Player::SpawnInfo collected_info)
{
    std::unique_ptr<Player, void (*)(Player *)> player {
        get_global_player_pool().spawn(
            {
                .position { 8.0, 82.0, 8.0 },
                .pitch { },
                .yaw { },
                .head_yaw { },
                .is_on_ground { false },
                .is_pushing_against_wall { false },
                .is_position_present { true },
                .is_rotation_present { true },
            },
            std::move(collected_info)),
        +[](Player *p) { get_global_player_pool().kill(*p); },
    };
    if (player == nullptr)
        co_return co_await disconnect::play(
            transport, "Sorry but the player pool is full.");

    std::vector<uint8_t> packet_vec;
    packet_vec.reserve(8192);
    FinalisationSignal fin;

    auto ret_due_to_fin = [&](const auto &fin_result) -> asio::awaitable<void>
    {
        co_await disconnect::play(transport,
            disconnect::fmt_reason(fin_result.fin_ec, fin_result.fin_ctx));
    };

    auto ret_args_emit_fin
        = [&](sys::error_code ec, std::string_view ctx) -> asio::awaitable<void>
    {
        fin.emit(ec, ctx);
        if (ec == asio::error::eof)
            transport.socket.shutdown(tcp::socket::shutdown_both);
        else
            co_await disconnect::play(
                transport, disconnect::fmt_reason(ec, ctx));
    };

    KeepAlive keep_alive(transport);
    asio::co_spawn(transport.socket.get_executor(),
        keep_alive.detached_clientbound_loop(fin),
        detached_rethrow_exceptions_token);

    PlayerPosRotUpdater player_posrot_updater;
    asio::co_spawn(transport.socket.get_executor(),
        player_posrot_updater.detached_sync_loop(transport, *player, fin),
        detached_rethrow_exceptions_token);

    asio::co_spawn(transport.socket.get_executor(),
        detached_initiation_process(transport, *player, fin),
        detached_rethrow_exceptions_token);

    for (;;)
    {
        packet_vec.clear();

        auto size_result = co_await awaitable_or_fin_result(
            fin, packetops::receive_packet_size(transport));
        if (!size_result.result)
            co_return co_await ret_due_to_fin(size_result);
        auto [ec, size] = *size_result.result;
        if (ec)
            co_return co_await ret_args_emit_fin(
                ec, "packetops::receive_packet_size");
        packet_vec.resize(size);

        auto recv_result = co_await awaitable_or_fin_result(fin,
            packetops::receive_raw_data(
                transport, size, asio::buffer(packet_vec)));
        if (!recv_result.result)
            co_return co_await ret_due_to_fin(recv_result);
        if (*recv_result.result)
            co_return co_await ret_args_emit_fin(
                *recv_result.result, "packetops::receive_raw_data");

        auto it = packet_vec.begin();
        auto packet_id = InlineTie(TieReturn, it, ec)
            = read_var<uint32_t>(it, packet_vec.end());
        if (ec)
            co_return co_await ret_args_emit_fin(ec, "parsing packet ID");
        std::span<const uint8_t> packet_data { &*it, &*packet_vec.end() };

        std::string_view ctx;

        if (packet_id == ServerboundChatMessagePacketID)
            std::tie(ec, ctx) = on_chat_message_packet(packet_data, *player);
        else if (packet_id == KeepAlive::ServerboundPacketID)
            std::tie(ec, ctx) = keep_alive.on_serverbound_packet(packet_data);
        else if (packet_id == SetPlayerPosPacketID
            || packet_id == SetPlayerPosRotPacketID
            || packet_id == SetPlayerRotPacketID
            || packet_id == SetPlayerMovementFlagsPacketID
            || packet_id == ConfirmTeleportationPacketID)
            std::tie(ec, ctx) = player_posrot_updater.on_posrot_packet(
                packet_id, packet_data, *player);
        else if (packet_id
            != ServerboundClientTickEndPacketID) // ignored due to its frequency
            std::println(
                "statecoroutines::play: received unused packet with ID 0x{:02X}",
                packet_id);

        if (ec)
            co_return co_await ret_args_emit_fin(ec, ctx);
    }
}
