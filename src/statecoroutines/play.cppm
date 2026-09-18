module;
#include <boost/asio.hpp>
#include <boost/asio/experimental/awaitable_operators.hpp>
#include <boost/asio/experimental/channel.hpp>
#include <boost/container/static_vector.hpp>
#include <boost/system.hpp>
#include <chrono>
#include <cstdint>
#include <print>
#include <random>
export module actualklasterkraft.statecoroutines.play;

import actualklasterkraft.bitfields;
import actualklasterkraft.disconnecthelpers;
import actualklasterkraft.errc;
import actualklasterkraft.formatters;
import actualklasterkraft.nbtbuilder;
import actualklasterkraft.packetops;
import actualklasterkraft.packetrouter;
import actualklasterkraft.protocolprimitives;
import actualklasterkraft.pubsub;
import actualklasterkraft.templates;
import actualklasterkraft.transport;
import actualklasterkraft.world.autogentest;
import actualklasterkraft.world.math;
import actualklasterkraft.world.player;

namespace asio = boost::asio;
namespace sys = boost::system;
using namespace protocolprimitives;
using namespace asio::experimental::awaitable_operators;
using namespace std::literals;
using asio::experimental::channel;
using asio::ip::tcp;
using ISI = std::istreambuf_iterator<char>;
using OSI = std::ostreambuf_iterator<char>;
using CanSig = asio::cancellation_signal;

static std::mt19937 g_rng { std::random_device { }() };
static std::uniform_int_distribution<uint64_t> g_u64_dist { };
static std::uniform_int_distribution<uint32_t> g_u32_dist { };

bool is_normal_shutdown(sys::error_code ec)
{
    return ec == asio::error::eof || ec == asio::error::operation_aborted
        || ec == asio::error::bad_descriptor;
}

/******************************************************************************/

constexpr int32_t ServerboundKeepAlivePacketID = 0x1C;
constexpr int32_t ClientboundKeepAlivePacketID = 0x2C;

asio::awaitable<void> keepalive_loop(
    Transport &transport, asio::streambuf &sb, PacketRouter &packet_router)
{
    asio::steady_timer send_timer(transport.socket.get_executor());
    channel<PacketRouter::OnPacketSignature> serverbound_keepalive_channel(
        transport.socket.get_executor());
    auto sub = packet_router.subscribe(ServerboundKeepAlivePacketID,
        [&](sys::error_code ec, uint32_t id)
        { serverbound_keepalive_channel.async_send(ec, id, asio::detached); });

    // If a payload is 0, the payload doesn't exist
    std::array<uint64_t, 10> active_payloads { };
    int timeout_counter { };
    sys::error_code ec { };

    for (;;)
    {
        send_timer.expires_after(1s);
        co_await send_timer.async_wait(asio::redirect_error(ec));
        if (ec)
            co_return co_await disconnect::play(
                transport, disconnect::fmt_desync(ec, "Keep Alive timer"));

        if (timeout_counter++ > 2)
            co_return co_await disconnect::play(
                transport, "Timeout (powered by ActualKlasterKraft)");

        std::array<uint8_t, 9> buf;
        buf[0] = ClientboundKeepAlivePacketID;
        uint64_t payload = g_u64_dist(g_rng);
        write_number<uint64_t>(buf.begin() + 1, payload);

        ec = co_await packetops::put(transport, asio::buffer(buf));
        if (is_normal_shutdown(ec))
            co_return;
        else if (ec)
            co_return co_await disconnect::play(transport,
                disconnect::fmt_desync(ec, "Clientbound Keep Alive"));

        for (uint64_t &active_payload : active_payloads)
        {
            if (active_payload == 0)
            {
                active_payload = payload;
                goto payload_placed;
            }
        }

        // No free slots for payloads, then replace the first
        active_payloads[0] = payload;

    payload_placed:
        co_await serverbound_keepalive_channel.async_receive(
            asio::redirect_error(ec));
        if (is_normal_shutdown(ec))
            co_return;
        else if (ec)
            co_return co_await disconnect::play(transport,
                disconnect::fmt_desync(ec, "Serverbound Keep Alive"));

        auto got_payload = InlineTie(TieReturn, std::ignore, ec)
            = read_number<uint64_t>(ISI(&sb), ISI());
        if (ec)
            co_return co_await disconnect::play(transport,
                disconnect::fmt_desync(ec, "Serverbound Keep Alive"));
        if (sb.size() > 0)
            co_return co_await disconnect::play(transport,
                disconnect::fmt_desync(MCProtocolError::ExcessPacketData,
                    "Serverbound Keep Alive"));

        for (auto &active_payload : active_payloads)
        {
            if (active_payload == got_payload)
            {
                active_payload = 0;
                timeout_counter = 0;
                goto payload_matched;
            }
        }
        co_return co_await disconnect::play(transport,
            disconnect::fmt_desync(MCProtocolError::CorrelationIDMismatch,
                "Serverbound Keep Alive"));

    payload_matched:
        continue;
    }
}

/******************************************************************************/

void put_login_packet(asio::streambuf &sb, uint32_t eid)
{
    // Values are mostly copied from Notchian server
    // id
    sb.sputc(0x31);
    // Entity ID
    write_number(OSI(&sb), eid);
    // Is hardcore
    sb.sputc(0);
    // Present dimension names
    sb.sputc(1); // count
    write_string(OSI(&sb), "minecraft:overworld");
    // Max players
    write_var<uint32_t>(OSI(&sb), get_global_player_pool().max_players());
    // View distance
    sb.sputc(10);
    // Simulation distance
    sb.sputc(10);
    // Reduced debug info
    sb.sputc(0);
    // Enable respawn screen
    sb.sputc(1);
    // Do limited crafting
    sb.sputc(0);
    // Dimension type player will be spawned into
    sb.sputc(0);
    // Dimension name player will be spawned into
    write_string(OSI(&sb), "minecraft:overworld");
    // First 8 bytes of seed's SHA-256
    write_number<uint64_t>(OSI(&sb), 6372804237062459062zu);
    // Gamemode
    sb.sputc(1); // Creative
    // Previous gamemode
    sb.sputc(0xFF); // Undefined
    // Is debug mode world (used to test resourcepacks, not our case)
    sb.sputc(0);
    // Is superflat world (affects rendering)
    sb.sputc(0);
    // Has death location (since disabled, death dimension name and death location fields are not present)
    sb.sputc(0);
    // Portal cooldown in ticks
    sb.sputc(0);
    // Sea level
    sb.sputc(63);
    // Enforce secure chat
    sb.sputc(0);
}

/******************************************************************************/

constexpr uint32_t UpdateEntityPosPacketID = 0x35,
                   UpdateEntityPosRotPacketID = 0x36,
                   UpdateEntityRotPacketID = 0x38;

int16_t calculate_axis_i16_delta(double current, double previous)
{
    double result = current * 4096.0 - previous * 4096.0;
    if (result > 32767.9)
        return 32767;
    else if (result < -32768.9)
        return -32768;
    else
        return static_cast<uint16_t>(result);
}

asio::awaitable<void> pull_posrot_loop(
    Transport &transport, Player &self, Player &target)
{
    for (;;)
    {
        PosRot previous_posrot = target.get_posrot();

        auto variant = co_await (
            target.wait_posrot_update(asio::as_tuple(asio::use_awaitable))
            || self.wait_player_exit_simulation_distance(
                asio::as_tuple(asio::use_awaitable)));
        if (auto *exited = std::get_if<1>(&variant))
        {
            auto &[ec, p] = *exited;
            if (!ec && p == &target)
                co_return;
            continue;
        }
        auto [ec, partial_posrot] = std::move(std::get<0>(variant));
        if (ec == MCProtocolError::EntityWasKilled)
            co_return;
        else if (ec)
            co_return co_await disconnect::play(transport,
                disconnect::fmt_desync(
                    ec, "while waiting Player.OnPosRotUpdate"));

        boost::container::static_vector<uint8_t, 32> vec;
        auto it = std::back_inserter(vec);
        write_var<uint32_t>(it,
            partial_posrot.is_position_present
                ? (partial_posrot.is_rotation_present
                          ? UpdateEntityPosRotPacketID
                          : UpdateEntityPosPacketID)
                : UpdateEntityRotPacketID);
        write_var<uint32_t>(it, target.get_eid());
        if (partial_posrot.is_position_present)
        {
            write_number(it,
                calculate_axis_i16_delta(
                    partial_posrot.position.x, previous_posrot.position.x));
            write_number(it,
                calculate_axis_i16_delta(
                    partial_posrot.position.y, previous_posrot.position.y));
            write_number(it,
                calculate_axis_i16_delta(
                    partial_posrot.position.z, previous_posrot.position.z));
        }
        if (partial_posrot.is_rotation_present)
        {
            write_angle256(it, partial_posrot.yaw);
            write_angle256(it, partial_posrot.pitch);
        }
        *it++ = partial_posrot.is_on_ground;

        ec = co_await packetops::put(transport, asio::buffer(vec));
        if (is_normal_shutdown(ec))
            co_return;
        else if (ec)
            co_return co_await disconnect::play(transport,
                disconnect::fmt_desync(
                    ec, "Update Player Position/Rotation/both"));
    }
}

/******************************************************************************/

constexpr uint32_t SetPlayerPosPacketID = 0x1E, SetPlayerPosRotPacketID = 0x1F,
                   SetPlayerRotPacketID = 0x20,
                   SetPlayerMovementFlagsPacketID = 0x21;

asio::awaitable<void> push_posrot_loop(Transport &transport,
    asio::streambuf &sb, PacketRouter &packet_router, Player &self)
{
    channel<PacketRouter::OnPacketSignature> channel(
        transport.socket.get_executor());
    auto send_cb = [&](sys::error_code ec, uint32_t id)
    { channel.async_send(ec, id, asio::detached); };

    auto sub_pos = packet_router.subscribe(SetPlayerPosPacketID, send_cb);
    auto sub_both = packet_router.subscribe(SetPlayerPosRotPacketID, send_cb);
    auto sub_rot = packet_router.subscribe(SetPlayerRotPacketID, send_cb);
    auto sub_none
        = packet_router.subscribe(SetPlayerMovementFlagsPacketID, send_cb);

    for (;;)
    {
        auto [ec, packet_id] = co_await channel.async_receive(asio::as_tuple);
        if (is_normal_shutdown(ec))
            co_return;
        else if (ec)
            co_return co_await disconnect::play(transport,
                disconnect::fmt_desync(
                    ec, "Set Player Position/Rotation/both/Movement Flags"));

        PosRot result;
        if (packet_id == SetPlayerPosPacketID
            || packet_id == SetPlayerPosRotPacketID)
        {
            for (auto *axis : result.position.to_ptr_array())
            {
                std::tie(*axis, std::ignore, ec)
                    = read_number<double>(ISI(&sb), ISI());
                if (ec)
                    co_return co_await disconnect::play(transport,
                        disconnect::fmt_desync(ec,
                            "Set Player Position/Rotation/both/Movement Flags"));
            }
            result.is_position_present = true;
        }
        if (packet_id == SetPlayerRotPacketID
            || packet_id == SetPlayerPosRotPacketID)
        {
            for (auto *axis : { &result.yaw, &result.pitch })
            {
                *axis
                    = Angle::from_degrees(InlineTie(TieReturn, std::ignore, ec)
                        = read_number<float>(ISI(&sb), ISI()));
                if (ec)
                    co_return co_await disconnect::play(transport,
                        disconnect::fmt_desync(ec,
                            "Set Player Position/Rotation/both/Movement Flags"));
            }
            result.is_rotation_present = true;
        }
        int flags = sb.sbumpc();
        if (flags < 0)
            co_return co_await disconnect::play(transport,
                disconnect::fmt_desync(MCProtocolError::UnsufficientPacketData,
                    "Set Player Position/Rotation/both/Movement Flags"));
        if (sb.sbumpc() >= 0)
            co_return co_await disconnect::play(transport,
                disconnect::fmt_desync(MCProtocolError::ExcessPacketData,
                    "Set Player Position/Rotation/both/Movement Flags"));
        result.is_on_ground = flags & 0b01;
        result.is_pushing_against_wall = flags & 0b10;

        self.update_posrot(result);
    }
}

/******************************************************************************/

asio::awaitable<void> send_spawn_entity_of_player(
    Transport &transport, Player &other)
{
    std::array<uint8_t, 64> buf;
    auto it = buf.data();

    *it++ = 0x01; // Spawn Entity
    it = write_var<uint32_t>(it, other.get_eid());
    it = std::ranges::copy(other.get_uuid(), it).out;
    it = write_var<uint32_t>(it, 155); // Entity Type = Player
    it = write_xyz(it, other.get_posrot().position);
    // Velocity is LpVec3, 0x00 means nought velocity
    *it++ = 0x00;
    it = write_angle256(it, other.get_posrot().pitch);
    it = write_angle256(it, other.get_posrot().yaw);
    // Head Yaw actually but we don't have it yet
    it = write_angle256(it, other.get_posrot().yaw);
    // Data, not for players
    it = write_var<uint32_t>(it, 0);

    auto ec = co_await packetops::put(
        transport, asio::buffer(buf.data(), it - buf.data()));
    if (is_normal_shutdown(ec))
        co_return;
    else if (ec)
        co_await disconnect::play(
            transport, disconnect::fmt_desync(ec, "Spawn Entity"));
}

asio::awaitable<void> send_remove_entity_of_player(
    Transport &transport, Player &other)
{
    // Remove Entities: Packet ID, count of Entity IDs
    std::array<uint8_t, 16> buf { 0x4D, 0x01 };
    auto end = write_var<uint32_t>(buf.data() + 2, other.get_eid());

    auto ec = co_await packetops::put(
        transport, asio::buffer(buf.data(), end - buf.data()));
    if (is_normal_shutdown(ec))
        co_return;
    else if (ec)
        co_await disconnect::play(
            transport, disconnect::fmt_desync(ec, "Remove Entities"));
}

/******************************************************************************/

void send_usual_message_to_all(std::string_view message)
{
    std::println("{}", message);

    auto text_component = std::make_shared<Player::TextComponentStorage>();
    text_component->reserve(message.size() + 1);

    NBTBuilder(std::back_inserter(*text_component))
        << nbttags::String << message;

    for (Player &player : get_global_player_pool().living_players())
    {
        player.send_chat_message(text_component);
    }
}

void send_yellow_message_to_all(std::string_view message)
{
    std::println("{}", message);

    auto text_component = std::make_shared<Player::TextComponentStorage>();
    text_component->reserve(message.size() + 24);

    NBTBuilder(std::back_inserter(*text_component))
        << nbttags::Compound << nbttags::String << "text" << message
        << nbttags::String << "color" << "yellow" << nbttags::End;

    for (Player &player : get_global_player_pool().living_players())
    {
        player.send_chat_message(text_component);
    }
}

/******************************************************************************/

asio::awaitable<void> chat_message_loop(Transport &transport,
    asio::streambuf &sb, PacketRouter &packet_router, Player &player)
{
    Signal<void(sys::error_code)> signal(transport.socket.get_executor());
    auto sub = packet_router.subscribe(
        0x09, [&](sys::error_code ec, uint32_t) { signal.emit(ec); });

    for (;;)
    {
        auto [ec] = co_await signal.wait(asio::as_tuple);
        if (is_normal_shutdown(ec))
            co_return;
        else if (ec)
            co_return co_await disconnect::play(
                transport, disconnect::fmt_desync(ec, "Chat Message"));

        auto message_len = InlineTie(TieReturn, std::ignore, std::ignore)
            = read_var<uint32_t>(ISI(&sb), ISI());
        if (message_len == 0) // it's 0 on errors too
            co_return co_await disconnect::play(
                transport, "You can't send an empty message. Don't abuse us.");
        if (message_len > 256)
            co_return co_await disconnect::play(transport,
                "You can't send a message longer than 256 characters. Don't abuse us.");

        std::string message = std::format("<{}> {:0256}", player.get_name(), 0);
        size_t prefix_len = player.get_name().size() + 3;

        if (message_len != sb.sgetn(message.data() + prefix_len, message_len))
            co_return co_await disconnect::play(transport,
                disconnect::fmt_desync(
                    MCProtocolError::UnsufficientPacketData, "Chat Message"));

        // Ignore Mojang crypto shit part of the packet
        sb.consume(sb.size());

        message.resize(prefix_len + message_len);
        send_usual_message_to_all(std::move(message));
    }
}

asio::awaitable<void> send_system_chat_message(Transport &transport,
    Player::SharedTextComponent text_component, bool is_overlay)
{
    uint8_t packet_id = 0x79, u8_is_overlay = is_overlay ? 0x01 : 0x00;
    auto ec = co_await packetops::put_va(transport, asio::buffer(&packet_id, 1),
        asio::buffer(*text_component), asio::buffer(&u8_is_overlay, 1));
    if (is_normal_shutdown(ec))
        co_return;
    else if (ec)
        co_await disconnect::play(
            transport, disconnect::fmt_desync(ec, "System Chat Message"));
}

/******************************************************************************/

export namespace statecoroutines
{
    asio::awaitable<void> play(Transport, std::string, std::array<uint8_t, 16>);
}

asio::awaitable<void> statecoroutines::play(
    Transport transport, std::string a_name, std::array<uint8_t, 16> a_uuid)
{
    std::unique_ptr<Player, void (*)(Player *)> player {
        get_global_player_pool().spawn(
            {
                .position { 8.0, 82.0, 8.0 },
                .pitch { },
                .yaw { },
                .is_on_ground { false },
                .is_pushing_against_wall { false },
                .is_position_present { true },
                .is_rotation_present { true },
            },
            { std::move(a_name), std::move(a_uuid) }),
        +[](Player *p) { get_global_player_pool().kill(*p); },
    };
    if (player == nullptr)
        co_return co_await disconnect::play(
            transport, "Sorry but the player pool is full.");

    asio::streambuf streambuf;

    PacketRouter packet_router(transport, streambuf);
    packet_router.begin_receiving();

    asio::co_spawn(transport.socket.get_executor(),
        keepalive_loop(transport, streambuf, packet_router), asio::detached);

    // Client Tick End 'no subscribers' warning spams stdout too hard, it's sent each tick
    auto client_tick_end_sub
        = packet_router.subscribe(0x0D, [&](sys::error_code, uint32_t) { });

    // Login (world state essentially)
    put_login_packet(streambuf, player->get_eid());
    auto ec = co_await packetops::put(transport, streambuf);
    if (is_normal_shutdown(ec))
        co_return;
    else if (ec)
        co_return co_await disconnect::play(
            transport, disconnect::fmt_desync(ec, "Login (play)"));

    // Synchronise Player Position
    uint32_t teleport_id = g_u32_dist(g_rng);

    streambuf.sputc(0x48); // packet id
    write_var<uint32_t>(OSI(&streambuf), teleport_id);
    // TODO: client somewhy ignores sent values and places the player at (0; 0; 0)
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
            disconnect::fmt_desync(ec, "Synchronise Player Position"));

    // Await for Confirm Teleportation
    {
        Signal<void(sys::error_code)> tmp_signal(
            transport.socket.get_executor());
        auto sub = packet_router.subscribe(
            0x00, [&](sys::error_code ec, uint32_t) { tmp_signal.emit(ec); });

        co_await tmp_signal.wait(asio::redirect_error(ec));
        if (is_normal_shutdown(ec))
            co_return;
        else if (ec)
            co_return co_await disconnect::play(
                transport, disconnect::fmt_desync(ec, "Confirm Teleportation"));

        uint32_t got_teleport_id = InlineTie(TieReturn, std::ignore, ec)
            = read_var<uint32_t>(ISI(&streambuf), ISI());
        if (ec)
            co_return co_await disconnect::play(
                transport, disconnect::fmt_desync(ec, "Confirm Teleportation"));

        if (got_teleport_id != teleport_id)
            co_return co_await disconnect::play(transport,
                disconnect::fmt_desync(MCProtocolError::CorrelationIDMismatch,
                    "Confirm Teleportation"));

        if (streambuf.size() > 0)
            co_return co_await disconnect::play(transport,
                disconnect::fmt_desync(MCProtocolError::ExcessPacketData,
                    "Confirm Teleportation"));
    }

    // Incoming messages soaking
    std::function<void(sys::error_code, Player::SharedTextComponent, bool)>
        when_saw_chat_message
        = [&](sys::error_code ec, Player::SharedTextComponent text_component,
              bool is_overlay)
    {
        if (ec)
            return;

        asio::co_spawn(transport.socket.get_executor(),
            send_system_chat_message(
                transport, std::move(text_component), is_overlay),
            asio::detached);

        player->wait_chat_message(when_saw_chat_message);
    };
    player->wait_chat_message(when_saw_chat_message);

    // Outcoming messages soaking
    asio::co_spawn(transport.socket.get_executor(),
        chat_message_loop(transport, streambuf, packet_router, *player),
        asio::detached);

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

    // send Game Event 'Start waiting for level chunks'
    streambuf.sputc(0x26); // packet id
    streambuf.sputc(13); // event id
    write_number(OSI(&streambuf), 0.f);

    ec = co_await packetops::put(transport, streambuf);
    if (is_normal_shutdown(ec))
        co_return;
    else if (ec)
        co_return co_await disconnect::play(
            transport, disconnect::fmt_desync(ec, "Game Event"));

    // send Set Centre Chunk
    streambuf.sputc(0x5E); // packet id
    write_var<int32_t>(OSI(&streambuf), 0); // X
    write_var<int32_t>(OSI(&streambuf), 0); // Z

    ec = co_await packetops::put(transport, streambuf);
    if (is_normal_shutdown(ec))
        co_return;
    else if (ec)
        co_return co_await disconnect::play(
            transport, disconnect::fmt_desync(ec, "Set Centre Chunk"));

    constexpr std::array center_block_states { chunkgen::GrassBlock,
        chunkgen::Air, chunkgen::GrassBlock, chunkgen::Air,
        chunkgen::GrassBlock, chunkgen::Air, chunkgen::GrassBlock,
        chunkgen::Air, chunkgen::GrassBlock, chunkgen::Air,
        chunkgen::GrassBlock, chunkgen::Air, chunkgen::GrassBlock,
        chunkgen::Air, chunkgen::GrassBlock, chunkgen::Air,
        chunkgen::GrassBlock, chunkgen::Air, chunkgen::GrassBlock,
        chunkgen::Air, chunkgen::GrassBlock, chunkgen::Air,
        chunkgen::GrassBlock, chunkgen::Air };

    for (int32_t x = -1; x < 2; ++x)
    {
        for (int32_t z = -1; z < 2; ++z)
        {
            auto [buf1, buf2]
                = chunkgen::single_valued_sectioned_chunk({ x, z },
                    x == 0 && z == 0 ? center_block_states
                                     : chunkgen::AirBlockStates);

            ec = co_await packetops::put_va(transport, buf1, buf2);
            if (is_normal_shutdown(ec))
                co_return;
            else if (ec)
                co_return co_await disconnect::play(transport,
                    disconnect::fmt_desync(ec, "Update Chunk and Light Data"));
        }
    }

    for (auto &other : get_global_player_pool().living_players())
    {
        if (player.get() == &other)
            continue;

        other.notify_player_entered_simulation_distance(*player);
        asio::co_spawn(transport.socket.get_executor(),
            pull_posrot_loop(transport, *player, other), asio::detached);

        co_await send_spawn_entity_of_player(transport, other);
    }

    std::function<void(sys::error_code, Player *)> when_saw_other_player
        = [&](sys::error_code ec, Player *other)
    {
        if (ec)
            return;

        other->notify_player_entered_simulation_distance(*player);
        asio::co_spawn(transport.socket.get_executor(),
            pull_posrot_loop(transport, *player, *other), asio::detached);

        asio::co_spawn(transport.socket.get_executor(),
            send_spawn_entity_of_player(transport, *other), asio::detached);

        player->wait_player_enter_simulation_distance(when_saw_other_player);
    };
    player->wait_player_enter_simulation_distance(when_saw_other_player);

    std::function<void(sys::error_code, Player *)> when_unsaw_other_player
        = [&](sys::error_code ec, Player *other)
    {
        if (ec)
            return;

        asio::co_spawn(transport.socket.get_executor(),
            send_remove_entity_of_player(transport, *other), asio::detached);
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
