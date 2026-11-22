module;
#include <boost/asio.hpp>
#include <boost/container/static_vector.hpp>
#include <cstddef>
#include <cstdint>
export module actualklasterkraft.net.play.initiation;

import actualklasterkraft.generic.completiontokens;
import actualklasterkraft.generic.errc;
import actualklasterkraft.generic.pubsub;
import actualklasterkraft.generic.templates;
import actualklasterkraft.data.protocolprimitives;
import actualklasterkraft.net.base.packetops;
import actualklasterkraft.net.base.transport;
import actualklasterkraft.net.play.chat;
import actualklasterkraft.net.play.chunkserialization;
import actualklasterkraft.net.play.entityplacement;
import actualklasterkraft.net.play.finalisation;
import actualklasterkraft.net.play.motion;
import actualklasterkraft.net.play.playerlist;
import actualklasterkraft.world.chunk;
import actualklasterkraft.world.player;

namespace sys = boost::system;
namespace asio = boost::asio;
using namespace protocolprimitives;
static constexpr auto AwTuple = asio::as_tuple(asio::use_awaitable);

constexpr uint32_t LoginPlayPacketID = 0x31, GameEventPacketID = 0x26,
                   SetCentreChunkPacketID = 0x5E,
                   ChunkDataUpdateLightPacketID = 0x2D;

auto put_login_packet(auto it, const Player &player)
{
    // Values are mostly copied from Notchian server
    // Packet ID
    it = write_var<uint32_t>(it, LoginPlayPacketID);
    // Entity ID
    it = write_number(it, player.get_eid());
    // Is hardcore
    *it++ = 0;
    // Present dimension names
    it = write_var<uint32_t>(it, 1); // count
    it = write_string(it, "minecraft:overworld");
    // Max players
    it = write_var<uint32_t>(it, get_global_player_pool().max_players());
    // View distance
    *it++ = player.get_view_distance();
    // Simulation distance
    *it++ = 8;
    // Reduced debug info
    *it++ = 0;
    // Enable respawn screen
    *it++ = 1;
    // Do limited crafting
    *it++ = 0;
    // Dimension type player will be spawned into
    *it++ = 0;
    // Dimension name player will be spawned into
    it = write_string(it, "minecraft:overworld");
    // First 8 bytes of seed's SHA-256
    it = write_number<uint64_t>(it, 6372804237062459062zu);
    // Gamemode
    *it++ = 1; // Creative
    // Previous gamemode
    *it++ = 0xFF; // Undefined
    // Is debug mode world (used to test resourcepacks)
    *it++ = 0;
    // Is superflat world (affects rendering)
    *it++ = 0;
    // Has death location (since disabled, death dimension name and death location fields are not present)
    *it++ = 0;
    // Portal cooldown in ticks
    *it++ = 0;
    // Sea level
    *it++ = 63;
    // Enforce secure chat
    *it++ = 0;

    return it;
}

asio::awaitable<void> detached_enter_simulation_distance_handling_loop(
    Transport &transport, Player &self, FinalisationSignal &fin)
{
    for (;;)
    {
        auto opt_ec_player = co_await awaitable_or_fin(
            fin, self.wait_player_enter_simulation_distance(AwTuple));
        if (!opt_ec_player)
            co_return;

        auto &[ec, other] = *opt_ec_player;
        if (ec)
            co_return fin.emit(ec, "Player.OnPlayerEnterSimulationDistance");

        asio::co_spawn(transport.socket.get_executor(),
            detached_pull_posrot_loop(transport, self, *other, fin),
            detached_rethrow_exceptions_token);

        auto opt_ec_ctx = co_await awaitable_or_fin(
            fin, send_spawn_entity_of_player(transport, *other));
        if (!opt_ec_ctx)
            co_return;

        auto ctx = InlineTie(ec, TieReturn) = *opt_ec_ctx;
        if (ec)
            co_return fin.emit(ec, ctx);
    }
}

asio::awaitable<void> detached_exit_simulation_distance_handling_loop(
    Transport &transport, Player &self, FinalisationSignal &fin)
{
    for (;;)
    {
        auto opt_ec_player = co_await awaitable_or_fin(
            fin, self.wait_player_exit_simulation_distance(AwTuple));
        if (!opt_ec_player)
            co_return;

        auto &[ec, other] = *opt_ec_player;
        if (ec)
            co_return fin.emit(ec, "Player.OnPlayerExitSimulationDistance");

        asio::co_spawn(transport.socket.get_executor(),
            awaitable_or_fin(
                fin, send_remove_entity_of_player(transport, *other)),
            [&fin](std::exception_ptr exc_ptr,
                std::optional<std::tuple<sys::error_code, std::string_view>>
                    result)
            {
                if (exc_ptr)
                    std::rethrow_exception(exc_ptr);
                if (!result)
                    return;
                auto &[ec, ctx] = *result;
                if (ec)
                    return fin.emit(ec, ctx);
            });
    }
}

export asio::awaitable<void> detached_initiation_process(
    Transport &transport, Player &player, FinalisationSignal &fin)
{
    boost::container::static_vector<uint8_t, 512> buf1;

    auto it1 = std::back_inserter(buf1);
    put_login_packet(it1, player);

    auto opt_ec = co_await awaitable_or_fin(
        fin, packetops::put(transport, asio::buffer(buf1)));
    if (!opt_ec)
        co_return;
    if (*opt_ec)
        co_return fin.emit(*opt_ec, "Login (play)");

    // This should lead to sending Synchronise Player Position
    auto posrot = player.get_posrot();
    posrot.force_sync = true;
    player.update_posrot(posrot);

    // Incoming messages soaking
    asio::co_spawn(transport.socket.get_executor(),
        detached_pull_chat_messages_loop(transport, player, fin),
        detached_rethrow_exceptions_token);

    if (!co_await awaitable_or_fin(fin, asio::post(AwTuple)))
        co_return;

    // Hello, player!
    send_yellow_message_to_all(
        std::format("{} joined the game", player.get_name()));
    player.wait_about_to_die(
        [&player](sys::error_code ec)
        {
            if (ec)
                return;

            send_yellow_message_to_all(
                std::format("{} left the game", player.get_name()));
        });

    auto opt_ec_ctx
        = co_await awaitable_or_fin(fin, init_tab_list(transport, player));
    if (!opt_ec_ctx)
        co_return;
    auto &[ec, ctx] = *opt_ec_ctx;
    if (ec)
        co_return fin.emit(ec, ctx);

    asio::co_spawn(transport.socket.get_executor(),
        detached_tab_list_loop(transport, player, fin),
        detached_rethrow_exceptions_token);

    // Game Event 'Start waiting for chunks'
    buf1.clear();
    write_var<uint32_t>(it1, GameEventPacketID);
    write_number<uint8_t>(it1, 13); // event ID
    write_number(it1, 0.f);

    opt_ec = co_await awaitable_or_fin(
        fin, packetops::put(transport, asio::buffer(buf1)));
    if (!opt_ec)
        co_return;
    if (*opt_ec)
        co_return fin.emit(*opt_ec, "Game Event");

    buf1.clear();
    write_var<uint32_t>(it1, SetCentreChunkPacketID);
    write_var<uint32_t>(it1, 0); // X
    write_var<uint32_t>(it1, 0); // Z

    opt_ec = co_await awaitable_or_fin(
        fin, packetops::put(transport, asio::buffer(buf1)));
    if (!opt_ec)
        co_return;
    if (*opt_ec)
        co_return fin.emit(*opt_ec, "Set Centre Chunk");

    std::array<Chunk *, 3> z0_chunks;
    z0_chunks[1] = get_global_chunk_pool().get({ 0, 0 });
    z0_chunks[0] = z0_chunks[1]->get_negative_x_neighbor();
    z0_chunks[2] = z0_chunks[1]->get_positive_x_neighbor();

    std::vector<uint8_t> buf2;
    buf2.reserve(65536);

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

            buf1.clear();
            buf1.push_back(ChunkDataUpdateLightPacketID);
            write_number(std::back_inserter(buf1), x);
            write_number(std::back_inserter(buf1), z);
            chunkserialization::heightmaps(std::back_inserter(buf1), *chunk);

            buf2.clear();
            chunkserialization::data(std::back_inserter(buf2), *chunk);
            write_var<uint32_t>(std::back_inserter(buf1), buf2.size());

            chunkserialization::block_entities(
                std::back_inserter(buf2), *chunk);
            chunkserialization::light(std::back_inserter(buf2), *chunk);

            opt_ec = co_await awaitable_or_fin(
                fin, packetops::put_va(transport, buf1, buf2));
            if (!opt_ec)
                co_return;
            if (*opt_ec)
                co_return fin.emit(*opt_ec, "Chunk Data & Update Light");
        }
    }

    buf2.clear();
    buf2.shrink_to_fit();

    for (auto &other : get_global_player_pool().living_players())
    {
        if (&player == &other)
            continue;

        other.notify_player_entered_simulation_distance(player);
        asio::co_spawn(transport.socket.get_executor(),
            detached_pull_posrot_loop(transport, player, other, fin),
            detached_rethrow_exceptions_token);

        opt_ec_ctx = co_await awaitable_or_fin(
            fin, send_spawn_entity_of_player(transport, other));
        if (!opt_ec_ctx)
            co_return;
        if (ec)
            co_return fin.emit(ec, ctx);
    }

    asio::co_spawn(transport.socket.get_executor(),
        detached_enter_simulation_distance_handling_loop(
            transport, player, fin),
        detached_rethrow_exceptions_token);

    asio::co_spawn(transport.socket.get_executor(),
        detached_exit_simulation_distance_handling_loop(transport, player, fin),
        detached_rethrow_exceptions_token);

    player.wait_about_to_die(
        [&player](sys::error_code ec)
        {
            if (ec)
                return;
            for (auto &other : get_global_player_pool().living_players())
            {
                if (&player == &other)
                    continue;
                other.notify_player_exited_simulation_distance(player);
            }
        });
}
