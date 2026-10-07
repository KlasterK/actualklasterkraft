module;
#include <boost/asio.hpp>
#include <boost/asio/experimental/awaitable_operators.hpp>
#include <boost/asio/experimental/channel.hpp>
#include <boost/container/static_vector.hpp>
#include <boost/system.hpp>
#include <chrono>
#include <cstdint>
#include <memory>
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
import actualklasterkraft.net.play.session;

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
    // NOTE: `new` instead of std::make_shared: MSVC's
    // enable_shared_from_this declares a hidden friend make_shared which
    // makes an unqualified/ADL call ambiguous with std::make_shared.
    auto session
        = std::shared_ptr<PlaySession>(new PlaySession(std::move(transport)));
    Transport &session_transport = session->transport;
    asio::streambuf &streambuf = session->streambuf;
    PacketRouter &packet_router = session->router;

    Player *raw_player = get_global_player_pool().spawn(
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
        std::move(collected_info));
    if (raw_player == nullptr)
    {
        co_await disconnect::play(
            session_transport, "Sorry but the player pool is full.");
        co_return;
    }
    session->player = raw_player;
    Player &player = *raw_player;

    // Join point: wait until all tracked tasks are gone, then kill the
    // player. Killing only after the join guarantees the pool slot cannot
    // be reused while a dangling loop still references this Player.
    auto shutdown = [&](bool kill) -> asio::awaitable<void>
    {
        session->dead = true;
        session->done.emit({ });
        sys::error_code cancel_ec;
        session_transport.socket.cancel(cancel_ec);
        // Re-emit periodically: belt and suspenders in case some wait
        // site misses the first emission (all known sites check `dead`,
        // but a late fix shouldn't turn into a hang).
        asio::steady_timer poll(session_transport.socket.get_executor());
        while (session->pending != 0)
        {
            poll.expires_after(std::chrono::milliseconds(100));
            co_await poll.async_wait(asio::as_tuple(asio::use_awaitable));
            if (session->pending == 0)
                break;
            session->done.emit({ });
        }
        if (kill)
        {
            for (auto &other : get_global_player_pool().living_players())
            {
                if (&player == &other)
                    continue;
                other.notify_player_exited_simulation_distance(player);
            }
            get_global_player_pool().kill(player);
        }
    };

    packet_router.begin_receiving(session);

    asio::co_spawn(session->transport.socket.get_executor(),
        keepalive_loop(session), detached_log_exceptions_token);

    // Client Tick End 'no subscribers' warning spams stdout too hard, it's sent each tick
    auto client_tick_end_sub
        = packet_router.subscribe(0x0D, [&](sys::error_code, uint32_t) { });

    // Login (world state essentially)
    put_login_packet(OSI(&streambuf), player);
    auto ec = co_await packetops::put(session_transport, streambuf);
    if (is_normal_shutdown(ec))
        co_return co_await shutdown(true);
    else if (ec)
    {
        co_await disconnect::play(
            session_transport, disconnect::fmt_reason(ec, "Login (play)"));
        co_return co_await shutdown(true);
    }

    // Synchronise Player Position
    uint32_t teleport_id = std::uniform_int_distribution<uint32_t> { }(g_rng);

    streambuf.sputc(0x48); // packet id
    write_var<uint32_t>(OSI(&streambuf), teleport_id);
    write_xyz(OSI(&streambuf), player.get_posrot().position);
    write_xyz(OSI(&streambuf), Vec3<double>()); // velocity
    write_number(OSI(&streambuf), player.get_posrot().yaw.as_degrees());
    write_number(OSI(&streambuf), player.get_posrot().pitch.as_degrees());
    TeleportFlags::IntT teleport_flags { 0 };
    write_number(OSI(&streambuf), teleport_flags);

    ec = co_await packetops::put(session_transport, streambuf);
    if (is_normal_shutdown(ec))
        co_return co_await shutdown(true);
    else if (ec)
    {
        co_await disconnect::play(session_transport,
            disconnect::fmt_reason(ec, "Synchronise Player Position"));
        co_return co_await shutdown(true);
    }

    // Await for Confirm Teleportation.
    // NOTE: `sub` unregisters itself on scope exit (PacketSubscription
    // fix). Previously the callback stayed in the router after this block
    // and fired later on a destroyed stack Signal -> crash on disconnect.
    {
        Signal<void(sys::error_code)> tmp_signal;
        auto sub = packet_router.subscribe(
            0x00, [&](sys::error_code ec, uint32_t) { tmp_signal.emit(ec); });

        // cancel_all may have already run (client disconnected while we
        // were sending the teleport): the signal will never fire.
        if (packet_router.dead())
            co_return co_await shutdown(true);

        if (session->dead)
            co_return co_await shutdown(true);

        auto variant
            = co_await (tmp_signal.wait(asio::as_tuple(asio::use_awaitable))
                || session->done.wait(asio::as_tuple(asio::use_awaitable)));
        if (std::get_if<1>(&variant))
            co_return co_await shutdown(true);
        ec = std::get<0>(std::get<0>(variant));
        if (is_normal_shutdown(ec))
            co_return co_await shutdown(true);
        else if (ec)
        {
            co_await disconnect::play(session_transport,
                disconnect::fmt_reason(ec, "Confirm Teleportation"));
            co_return co_await shutdown(true);
        }

        uint32_t got_teleport_id = InlineTie(TieReturn, std::ignore, ec)
            = read_var<uint32_t>(ISI(&streambuf), ISI());
        if (ec)
        {
            co_await disconnect::play(session_transport,
                disconnect::fmt_reason(ec, "Confirm Teleportation"));
            co_return co_await shutdown(true);
        }

        if (got_teleport_id != teleport_id)
        {
            co_await disconnect::play(session_transport,
                disconnect::fmt_reason(MCProtocolError::CorrelationIDMismatch,
                    "Confirm Teleportation"));
            co_return co_await shutdown(true);
        }

        if (streambuf.size() > 0)
        {
            co_await disconnect::play(session_transport,
                disconnect::fmt_reason(MCProtocolError::ExcessPacketData,
                    "Confirm Teleportation"));
            co_return co_await shutdown(true);
        }
    }

    // Incoming messages soaking.
    // Captures `session` (shared) instead of raw `transport&` so the
    // callback stays valid until the player dies (which fires it with
    // EntityWasKilled and lets it drop the reference).
    std::function<void(sys::error_code, Player::SharedTextComponent, bool)>
        when_saw_chat_message
        = [session, p = session->player, &when_saw_chat_message](
              sys::error_code ec, Player::SharedTextComponent text_component,
              bool is_overlay)
    {
        if (ec)
            return;

        asio::co_spawn(session->transport.socket.get_executor(),
            send_system_chat_message(
                session, std::move(text_component), is_overlay),
            detached_log_exceptions_token);

        p->wait_chat_message(when_saw_chat_message);
    };
    player.wait_chat_message(when_saw_chat_message);

    // Outcoming messages soaking
    asio::co_spawn(session->transport.socket.get_executor(),
        chat_message_loop(session), detached_log_exceptions_token);

    // Hello, player!
    send_yellow_message_to_all(
        std::format("{} joined the game", player.get_name()));
    player.wait_about_to_die(
        [p = session->player](sys::error_code ec)
        {
            if (ec)
                return;
            send_yellow_message_to_all(
                std::format("{} left the game", p->get_name()));
        });

    if (!co_await init_tab_list(session))
        co_return co_await shutdown(true);

    // send Game Event 'Start waiting for level chunks'
    streambuf.sputc(0x26); // packet id
    streambuf.sputc(13); // event id
    write_number(OSI(&streambuf), 0.f);

    ec = co_await packetops::put(session_transport, streambuf);
    if (is_normal_shutdown(ec))
        co_return co_await shutdown(true);
    else if (ec)
    {
        co_await disconnect::play(
            session_transport, disconnect::fmt_reason(ec, "Game Event"));
        co_return co_await shutdown(true);
    }

    // send Set Centre Chunk
    streambuf.sputc(0x5E); // packet id
    write_var<int32_t>(OSI(&streambuf), 0); // X
    write_var<int32_t>(OSI(&streambuf), 0); // Z

    ec = co_await packetops::put(session_transport, streambuf);
    if (is_normal_shutdown(ec))
        co_return co_await shutdown(true);
    else if (ec)
    {
        co_await disconnect::play(
            session_transport, disconnect::fmt_reason(ec, "Set Centre Chunk"));
        co_return co_await shutdown(true);
    }

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

            std::vector<uint8_t> buf1;
            buf1.reserve(0x400);

            buf1.push_back(0x2D); // Chunk Data & Update Light
            write_number(std::back_inserter(buf1), x);
            write_number(std::back_inserter(buf1), z);
            chunkserialization::heightmaps(std::back_inserter(buf1), *chunk);

            std::vector<uint8_t> buf2;
            buf2.reserve(0x400);
            chunkserialization::data(std::back_inserter(buf2), *chunk);
            write_var<uint32_t>(std::back_inserter(buf1), buf2.size());

            chunkserialization::block_entities(
                std::back_inserter(buf2), *chunk);
            chunkserialization::light(std::back_inserter(buf2), *chunk);

            ec = co_await packetops::put_va(session_transport, buf1, buf2);
            if (is_normal_shutdown(ec))
                co_return co_await shutdown(true);
            else if (ec)
            {
                co_await disconnect::play(session_transport,
                    disconnect::fmt_reason(ec, "Chunk Data & Update Light"));
                co_return co_await shutdown(true);
            }
        }
    }

    for (auto &other : get_global_player_pool().living_players())
    {
        if (&player == &other)
            continue;

        other.notify_player_entered_simulation_distance(player);
        asio::co_spawn(session->transport.socket.get_executor(),
            pull_posrot_loop(session, other), detached_log_exceptions_token);

        co_await send_spawn_entity_of_player(session, other);
    }

    std::function<void(sys::error_code, Player *)> when_saw_other_player
        = [session, &when_saw_other_player](sys::error_code ec, Player *other)
    {
        if (ec || !other)
            return;

        other->notify_player_entered_simulation_distance(*session->player);
        asio::co_spawn(session->transport.socket.get_executor(),
            pull_posrot_loop(session, *other), detached_log_exceptions_token);
        asio::co_spawn(session->transport.socket.get_executor(),
            send_spawn_entity_of_player(session, *other),
            detached_log_exceptions_token);

        session->player->wait_player_enter_simulation_distance(
            when_saw_other_player);
    };
    player.wait_player_enter_simulation_distance(when_saw_other_player);

    std::function<void(sys::error_code, Player *)> when_unsaw_other_player
        = [session, &when_unsaw_other_player](sys::error_code ec, Player *other)
    {
        if (ec || !other)
            return;

        asio::co_spawn(session->transport.socket.get_executor(),
            send_remove_entity_of_player(session, *other),
            detached_log_exceptions_token);
        session->player->wait_player_exit_simulation_distance(
            when_unsaw_other_player);
    };
    player.wait_player_exit_simulation_distance(when_unsaw_other_player);

    co_await push_posrot_loop(session);

    co_await shutdown(true);
}
