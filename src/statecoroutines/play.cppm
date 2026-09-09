module;
#include <boost/asio.hpp>
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
import actualklasterkraft.templates;
import actualklasterkraft.transport;
import actualklasterkraft.world.autogentest;

namespace asio = boost::asio;
namespace sys = boost::system;
using asio::ip::tcp;
using ISI = std::istreambuf_iterator<char>;
using OSI = std::ostreambuf_iterator<char>;
using namespace protocolprimitives;

static std::mt19937 g_rng { std::random_device { }() };
static std::uniform_int_distribution<uint64_t> g_u64_dist { };
static std::uniform_int_distribution<uint32_t> g_u32_dist { };

class KeepAlive
{
public:
    static constexpr int32_t ServerboundPacketID = 0x1C;
    static constexpr int32_t ClientboundPacketID = 0x2C;

public:
    KeepAlive(
        Transport &transport, asio::streambuf &sb, PacketRouter &packet_router);
    asio::awaitable<void> keepalive_loop();

private:
    Transport &m_transport;
    asio::streambuf &m_streambuf;
    asio::steady_timer m_send_timer;
    PacketRouter::PacketChannel m_serverbound_keepalive_channel;
    PacketRouter::SubscriptionGuard m_subguard;

    // If a payload is 0, the payload doesn't exist
    std::array<uint64_t, 10> m_active_payloads { };
    int m_timeout_counter { };
};

void put_login_packet(asio::streambuf &sbuf);

export namespace statecoroutines
{
    asio::awaitable<void> play(Transport transport, std::string player_name,
        std::array<uint8_t, 16> player_uuid);
}

///////////////////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////

KeepAlive::KeepAlive(
    Transport &transport, asio::streambuf &sb, PacketRouter &packet_router)
    : m_transport(transport)
    , m_streambuf(sb)
    , m_send_timer(transport.socket.get_executor(), std::chrono::seconds(1))
    , m_serverbound_keepalive_channel(transport.socket.get_executor())
    , m_subguard(packet_router.subscribe(
          m_serverbound_keepalive_channel, ServerboundPacketID))
{
}

asio::awaitable<void> KeepAlive::keepalive_loop()
{
    for (sys::error_code ec { };;)
    {
        if (!m_transport.socket.is_open())
            co_return;

        co_await m_send_timer.async_wait(asio::redirect_error(ec));
        if (ec)
            co_return co_await disconnect::play(
                m_transport, disconnect::fmt_desync(ec, "Keep Alive timer"));

        if (m_timeout_counter++ > 2)
            co_return co_await disconnect::play(
                m_transport, "Timeout (powered by ActualKlasterKraft)");

        std::array<uint8_t, 9> buf;
        buf[0] = ClientboundPacketID;
        uint64_t payload = g_u64_dist(g_rng);
        write_integer<uint64_t>(buf.begin() + 1, payload);

        ec = co_await packetops::put(m_transport, asio::buffer(buf));
        if (ec)
            co_return co_await disconnect::play(m_transport,
                disconnect::fmt_desync(ec, "Clientbound Keep Alive"));

        for (uint64_t &active_payload : m_active_payloads)
        {
            if (active_payload == 0)
            {
                active_payload = payload;
                goto payload_placed;
            }
        }

        // No free slots for payloads, then replace the first
        m_active_payloads[0] = payload;

    payload_placed:
        co_await m_serverbound_keepalive_channel.async_receive(
            asio::redirect_error(ec));
        if (ec)
            co_return co_await disconnect::play(m_transport,
                disconnect::fmt_desync(ec, "Serverbound Keep Alive"));

        auto got_payload = InlineTie(TieReturn, std::ignore, ec)
            = read_integer<uint64_t>(ISI(&m_streambuf), ISI());
        if (ec)
            co_return co_await disconnect::play(m_transport,
                disconnect::fmt_desync(ec, "Serverbound Keep Alive"));
        if (m_streambuf.size() > 0)
            co_return co_await disconnect::play(m_transport,
                disconnect::fmt_desync(MCProtocolError::ExcessPacketData,
                    "Serverbound Keep Alive"));

        for (auto &active_payload : m_active_payloads)
        {
            if (active_payload == got_payload)
            {
                active_payload = 0;
                m_timeout_counter = 0;
                goto payload_matched;
            }
        }
        co_return co_await disconnect::play(m_transport,
            disconnect::fmt_desync(MCProtocolError::CorrelationIDMismatch,
                "Serverbound Keep Alive"));

    payload_matched:
        continue;
    }
}

void put_login_packet(asio::streambuf &sb)
{
    // Values are mostly copied from Notchian server
    // id
    sb.sputc(0x31);
    // Entity ID
    write_integer<int32_t>(OSI(&sb), 1);
    // Is hardcore
    sb.sputc(0);
    // Present dimension names
    sb.sputc(1); // count
    write_string(OSI(&sb), "minecraft:overworld");
    // Max players
    write_var<uint32_t>(OSI(&sb), 20);
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
    write_integer<uint64_t>(OSI(&sb), 123456789);
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

asio::awaitable<void> statecoroutines::play(Transport transport,
    std::string player_name, std::array<uint8_t, 16> player_uuid)
{
    asio::streambuf streambuf;

    PacketRouter packet_router(transport, streambuf,
        [&](sys::error_code ec)
        {
            asio::co_spawn(transport.socket.get_executor(),
                disconnect::play(
                    transport, disconnect::fmt_desync(ec, "PacketRouter")),
                asio::detached);
        });
    packet_router.begin_receiving();

    KeepAlive keep_alive { transport, streambuf, packet_router };
    asio::co_spawn(transport.socket.get_executor(), keep_alive.keepalive_loop(),
        asio::detached);

    // Login (world state essentially)
    put_login_packet(streambuf);
    auto ec = co_await packetops::put(transport, streambuf);
    if (ec)
        co_return co_await disconnect::play(
            transport, disconnect::fmt_desync(ec, "Login the packet"));

    // Synchronise Player Position
    uint32_t teleport_id = g_u32_dist(g_rng);

    streambuf.sputc(0x48); // packet id
    write_var<uint32_t>(OSI(&streambuf), teleport_id);
    // position, velocity
    // TODO: client somewhy ignores sent values and places the player at (0; 0; 0)
    for (double value : { 8.0, 82.0, 8.0, 0.0, 0.0, 0.0 })
        write_real(OSI(&streambuf), value);
    // yaw, pitch
    for (float value : { 0.f, 0.f }) // looking towards positive Z
        write_real(OSI(&streambuf), value);
    TeleportFlags::IntT teleport_flags { 0 };
    write_integer(OSI(&streambuf), teleport_flags);

    ec = co_await packetops::put(transport, streambuf);
    if (ec)
        co_return co_await disconnect::play(transport,
            disconnect::fmt_desync(ec, "Synchronise Player Position"));

    // Await for Confirm Teleportation
    {
        PacketRouter::PacketChannel channel { transport.socket.get_executor() };
        auto sub = packet_router.subscribe(channel, 0x0);

        co_await channel.async_receive(asio::redirect_error(ec));
        if (ec)
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

    // send Game Event 'Start waiting for level chunks'
    streambuf.sputc(0x26); // packet id
    streambuf.sputc(13); // event id
    write_real(OSI(&streambuf), 0.f);

    std::println("\t{} joined the game", player_name);

    ec = co_await packetops::put(transport, streambuf);
    if (ec)
        co_return co_await disconnect::play(
            transport, disconnect::fmt_desync(ec, "Game Event"));

    // send Set Center Chunk
    streambuf.sputc(0x5E); // packet id
    write_var<int32_t>(OSI(&streambuf), 0); // X
    write_var<int32_t>(OSI(&streambuf), 0); // Z

    ec = co_await packetops::put(transport, streambuf);
    if (ec)
        co_return co_await disconnect::play(
            transport, disconnect::fmt_desync(ec, "Set Center Chunk"));

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
            const auto &block_states = x == 0 && z == 0
                ? center_block_states
                : chunkgen::AirBlockStates;
            auto [buf1, buf2]
                = chunkgen::single_valued_sectioned_chunk(x, z, block_states);

            ec = co_await packetops::put_va(transport, buf1, buf2);
            if (ec)
                co_return co_await disconnect::play(transport,
                    disconnect::fmt_desync(ec, "Update Chunk and Light Data"));
        }
    }

    // Do nothing until the socket closes
    asio::steady_timer timer(
        transport.socket.get_executor(), std::chrono::seconds(1));
    while (transport.socket.is_open())
    {
        co_await timer.async_wait(asio::redirect_error(ec));
        if (ec)
            co_return co_await disconnect::play(
                transport, disconnect::fmt_desync(ec, "timer"));
    }

    std::println("\t{} left the game", player_name);
    std::println("Connection {} closed", transport.remote_endpoint_copy);
}
