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
import actualklasterkraft.net.play.initiation;
import actualklasterkraft.net.play.keepalive;
import actualklasterkraft.net.play.motion;
import actualklasterkraft.net.play.packetrouter;
import actualklasterkraft.net.play.playerlist;

namespace asio = boost::asio;
namespace sys = boost::system;
using namespace protocolprimitives;
using namespace asio::experimental::awaitable_operators;
using namespace std::literals;
using asio::experimental::channel;
using asio::ip::tcp;
using ISI = std::istreambuf_iterator<char>;
using OSI = std::ostreambuf_iterator<char>;

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

    asio::streambuf streambuf;

    PacketRouter packet_router(transport, streambuf);
    packet_router.begin_receiving();

    asio::co_spawn(transport.socket.get_executor(),
        keepalive_loop(transport, streambuf, packet_router),
        detached_log_exceptions_token);

    // Client Tick End 'no subscribers' warning spams stdout too hard, it's sent each tick
    auto client_tick_end_sub
        = packet_router.subscribe(0x0D, [&](sys::error_code, uint32_t) { });

    // Login (world state essentially)
    put_login_packet(OSI(&streambuf), *player);
    auto ec = co_await packetops::put(transport, streambuf);
    if (is_normal_shutdown(ec))
        co_return;
    else if (ec)
        co_return co_await disconnect::play(
            transport, disconnect::fmt_reason(ec, "Login (play)"));

    // Synchronise Player Position
    uint32_t teleport_id = std::uniform_int_distribution<uint32_t> { }(g_rng);

    streambuf.sputc(0x48); // packet id
    write_var<uint32_t>(OSI(&streambuf), teleport_id);
    write_xyz(OSI(&streambuf), player->get_posrot().position);
    write_xyz(OSI(&streambuf), Vec3<double>()); // velocity
    write_number(OSI(&streambuf), player->get_posrot().yaw.as_degrees());
    write_number(OSI(&streambuf), player->get_posrot().pitch.as_degrees());
    TeleportFlags::IntT teleport_flags { 0 };
    write_number(OSI(&streambuf), teleport_flags);

    ec = co_await packetops::put(transport, streambuf);
    if (is_normal_shutdown(ec))
        co_return;
    else if (ec)
        co_return co_await disconnect::play(transport,
            disconnect::fmt_reason(ec, "Synchronise Player Position"));

    // Await for Confirm Teleportation
    {
        Signal<void(sys::error_code)> tmp_signal;
        auto sub = packet_router.subscribe(
            0x00, [&](sys::error_code ec, uint32_t) { tmp_signal.emit(ec); });

        co_await tmp_signal.wait(asio::redirect_error(ec));
        if (is_normal_shutdown(ec))
            co_return;
        else if (ec)
            co_return co_await disconnect::play(
                transport, disconnect::fmt_reason(ec, "Confirm Teleportation"));

        uint32_t got_teleport_id = InlineTie(TieReturn, std::ignore, ec)
            = read_var<uint32_t>(ISI(&streambuf), ISI());
        if (ec)
            co_return co_await disconnect::play(
                transport, disconnect::fmt_reason(ec, "Confirm Teleportation"));

        if (got_teleport_id != teleport_id)
            co_return co_await disconnect::play(transport,
                disconnect::fmt_reason(MCProtocolError::CorrelationIDMismatch,
                    "Confirm Teleportation"));

        if (streambuf.size() > 0)
            co_return co_await disconnect::play(transport,
                disconnect::fmt_reason(MCProtocolError::ExcessPacketData,
                    "Confirm Teleportation"));
    }

    // Incoming messages soaking
    std::function<void(sys::error_code, Player::SharedTextComponent, bool)>
        when_saw_chat_message
        = [&, p = player.get()](sys::error_code ec,
              Player::SharedTextComponent text_component, bool is_overlay)
    {
        if (ec)
            return;

        asio::co_spawn(transport.socket.get_executor(),
            send_system_chat_message(
                transport, std::move(text_component), is_overlay),
            detached_log_exceptions_token);

        p->wait_chat_message(when_saw_chat_message);
    };
    player->wait_chat_message(when_saw_chat_message);

    // Outcoming messages soaking
    asio::co_spawn(transport.socket.get_executor(),
        chat_message_loop(transport, streambuf, packet_router, *player),
        detached_log_exceptions_token);

    // Hello, player!
    send_yellow_message_to_all(
        std::format("{} joined the game", player->get_name()));
    player->wait_about_to_die(
        [p = player.get()](sys::error_code ec)
        {
            if (ec)
                return;
            send_yellow_message_to_all(
                std::format("{} left the game", p->get_name()));
        });

    if (!co_await init_tab_list(transport, *player))
        co_return;

    // send Game Event 'Start waiting for level chunks'
    streambuf.sputc(0x26); // packet id
    streambuf.sputc(13); // event id
    write_number(OSI(&streambuf), 0.f);

    ec = co_await packetops::put(transport, streambuf);
    if (is_normal_shutdown(ec))
        co_return;
    else if (ec)
        co_return co_await disconnect::play(
            transport, disconnect::fmt_reason(ec, "Game Event"));

    // send Set Centre Chunk
    streambuf.sputc(0x5E); // packet id
    write_var<int32_t>(OSI(&streambuf), 0); // X
    write_var<int32_t>(OSI(&streambuf), 0); // Z

    ec = co_await packetops::put(transport, streambuf);
    if (is_normal_shutdown(ec))
        co_return;
    else if (ec)
        co_return co_await disconnect::play(
            transport, disconnect::fmt_reason(ec, "Set Centre Chunk"));

    std::array<Chunk *, 3> z0_chunks;
    z0_chunks[1] = get_global_chunk_pool().get({ 0, 0 });
    z0_chunks[0] = z0_chunks[1]->get_negative_x_neighbor();
    z0_chunks[2] = z0_chunks[1]->get_positive_x_neighbor();

    for (int32_t x = -1; x < 2; ++x)
    {
        for (int32_t z = -1; z < 2; ++z)
        {
            Chunk *chunk { };
            if (z == -1)
                chunk = z0_chunks[x + 1]->get_negative_z_neighbor();
            else if (z == 0)
                chunk = z0_chunks[x + 1];
            else
                chunk = z0_chunks[x + 1]->get_positive_z_neighbor();

            boost::container::static_vector<uint8_t, 32> buf1;
            buf1.push_back(0x2D); // Chunk Data & Update Light
            write_number(std::back_inserter(buf1), x);
            write_number(std::back_inserter(buf1), z);
            chunkserialization::heightmaps(std::back_inserter(buf1), *chunk);

            boost::container::static_vector<uint8_t, 4096> buf2;
            chunkserialization::data(std::back_inserter(buf2), *chunk);
            write_var<uint32_t>(std::back_inserter(buf1), buf2.size());

            chunkserialization::block_entities(
                std::back_inserter(buf2), *chunk);
            chunkserialization::light(std::back_inserter(buf2), *chunk);

            ec = co_await packetops::put_va(transport, buf1, buf2);
            if (is_normal_shutdown(ec))
                co_return;
            else if (ec)
                co_return co_await disconnect::play(transport,
                    disconnect::fmt_reason(ec, "Chunk Data & Update Light"));
        }
    }

    for (auto &other : get_global_player_pool().living_players())
    {
        if (player.get() == &other)
            continue;

        other.notify_player_entered_simulation_distance(*player);
        asio::co_spawn(transport.socket.get_executor(),
            pull_posrot_loop(transport, *player, other),
            detached_log_exceptions_token);

        co_await send_spawn_entity_of_player(transport, other);
    }

    std::function<void(sys::error_code, Player *)> when_saw_other_player
        = [&](sys::error_code ec, Player *other)
    {
        if (ec)
            return;

        other->notify_player_entered_simulation_distance(*player);
        asio::co_spawn(transport.socket.get_executor(),
            pull_posrot_loop(transport, *player, *other),
            detached_log_exceptions_token);

        asio::co_spawn(transport.socket.get_executor(),
            send_spawn_entity_of_player(transport, *other),
            detached_log_exceptions_token);

        player->wait_player_enter_simulation_distance(when_saw_other_player);
    };
    player->wait_player_enter_simulation_distance(when_saw_other_player);

    std::function<void(sys::error_code, Player *)> when_unsaw_other_player
        = [&](sys::error_code ec, Player *other)
    {
        if (ec)
            return;

        asio::co_spawn(transport.socket.get_executor(),
            send_remove_entity_of_player(transport, *other),
            detached_log_exceptions_token);
        player->wait_player_exit_simulation_distance(when_unsaw_other_player);
    };
    player->wait_player_exit_simulation_distance(when_unsaw_other_player);

    co_await push_posrot_loop(transport, streambuf, packet_router, *player);

    for (auto &other : get_global_player_pool().living_players())
    {
        if (player.get() == &other)
            continue;
        other.notify_player_exited_simulation_distance(*player);
    }
}
