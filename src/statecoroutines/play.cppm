module;
#include <boost/asio.hpp>
#include <boost/asio/redirect_error.hpp>
#include <boost/intrusive_ptr.hpp>
#include <boost/smart_ptr/intrusive_ref_counter.hpp>
#include <chrono>
#include <cstdint>
#include <functional>
#include <print>
#include <random>
export module actualklasterkraft.statecoroutines.play;

import actualklasterkraft.bitfields;
import actualklasterkraft.disconnecthelpers;
import actualklasterkraft.errc;
import actualklasterkraft.packetops;
import actualklasterkraft.packetrouter;
import actualklasterkraft.session;
import actualklasterkraft.streambufops;
import actualklasterkraft.formatters;
import actualklasterkraft.nbtbuilder;

namespace asio = boost::asio;
namespace sys = boost::system;

static std::mt19937 g_rng { std::random_device { }() };
static std::uniform_int_distribution<uint64_t> g_u64_dist { };
static std::uniform_int_distribution<int32_t> g_i32_dist { };

class KeepAlive
{
public:
    static constexpr int32_t ServerboundPacketID = 0x1C;
    static constexpr int32_t ClientboundPacketID = 0x2C;

public:
    KeepAlive(
        boost::intrusive_ptr<Session> session, PacketRouter &packet_router);
    asio::awaitable<void> keepalive_loop();

private:
    boost::intrusive_ptr<Session> m_session;
    asio::steady_timer m_send_timer;
    PacketRouter::PacketChannel m_serverbound_keepalive_channel;
    PacketRouter::SubscriptionGuard m_subguard;

    // If a payload is 0, the payload doesn't exist
    std::array<uint64_t, 10> m_active_payloads { };
    int m_timeout_counter { };
};

void put_login_packet(asio::streambuf &sbuf);

asio::awaitable<void> graceful_disconnect(
    Session &session, std::string_view sv);

export namespace statecoroutines
{
    asio::awaitable<void> play(boost::intrusive_ptr<Session> session,
        std::string &&player_name, std::array<uint8_t, 16> &&player_uuid);
}

//
//
//
//
//
//
//
//
//
//

KeepAlive::KeepAlive(
    boost::intrusive_ptr<Session> session, PacketRouter &packet_router)
    : m_session(session)
    , m_send_timer(session->get_io(), std::chrono::seconds(1))
    , m_serverbound_keepalive_channel(session->get_io())
    , m_subguard(packet_router.subscribe(
          m_serverbound_keepalive_channel, ServerboundPacketID))
{
}

asio::awaitable<void> KeepAlive::keepalive_loop()
{
    for (sys::error_code ec { };;)
    {
        if (!m_session->get_socket().is_open())
            co_return;

        co_await m_send_timer.async_wait(asio::redirect_error(ec));
        if (ec)
            co_return co_await disconnect::play(
                *m_session, disconnect::fmt_desync(ec, "Keep Alive timer"));

        if (m_timeout_counter++ > 2)
            co_return co_await disconnect::play(
                *m_session, "Timeout (powered by ActualKlasterKraft)");

        m_session->get_streambuf().sputc(ClientboundPacketID);
        uint64_t payload = g_u64_dist(g_rng);
        streambufops::write_integer<uint64_t>(
            m_session->get_streambuf(), payload);

        ec = co_await packetops::flush_packet(*m_session);
        if (ec)
            co_return co_await disconnect::play(*m_session,
                disconnect::fmt_desync(ec, "Clienbound Keep Alive"));

        for (uint64_t &active_payload : m_active_payloads)
        {
            if (active_payload == 0)
            {
                active_payload = payload;
                goto finish;
            }
        }

        // No free slots for payloads, then replace the first
        m_active_payloads[0] = payload;

    finish:
        co_await m_serverbound_keepalive_channel.async_receive(
            asio::redirect_error(ec));
        if (ec)
            co_return co_await disconnect::play(*m_session,
                disconnect::fmt_desync(ec, "Serverbound Keep Alive"));

        auto got_payload = streambufops::read_integer<uint64_t>(
            m_session->get_streambuf(), ec);
        if (got_payload != payload)
            co_return co_await disconnect::play(*m_session,
                disconnect::fmt_desync(MCProtocolError::CorrelationIDMismatch,
                    "Serverbound Keep Alive"));

        if (m_session->get_streambuf().size() > 0)
            co_return co_await disconnect::play(*m_session,
                disconnect::fmt_desync(MCProtocolError::ExcessPacketData,
                    "Serverbound Keep Alive"));

        for (auto &active_payload : m_active_payloads)
        {
            if (active_payload == got_payload)
            {
                active_payload = 0;
                m_timeout_counter = 0;
                break;
            }
        }
    }
}

void put_login_packet(asio::streambuf &sbuf)
{
    // Values are mostly copied from Notchian server
    // id
    sbuf.sputc(0x31);
    // Entity ID
    streambufops::write_integer<int32_t>(sbuf, 1);
    // Is hardcore
    sbuf.sputc(0);
    // Present dimension names
    sbuf.sputc(1); // count
    streambufops::write_string(sbuf, "minecraft:overworld");
    // Max players
    streambufops::write_v32(sbuf, 20);
    // View distance
    sbuf.sputc(10);
    // Simulation distance
    sbuf.sputc(10);
    // Reduced debug info
    sbuf.sputc(0);
    // Enable respawn screen
    sbuf.sputc(1);
    // Do limited crafting
    sbuf.sputc(0);
    // Dimension type player will be spawned into
    sbuf.sputc(0);
    // Dimension name player will be spawned into
    streambufops::write_string(sbuf, "minecraft:overworld");
    // First 8 bytes of seed's SHA-256
    streambufops::write_integer<uint64_t>(sbuf, 123456789);
    // Gamemode
    sbuf.sputc(0); // Survival
    // Previous gamemode
    sbuf.sputc(0xFF); // Undefined
    // Is debug mode world (used to test resourcepacks, not our case)
    sbuf.sputc(0);
    // Is superflat world (affects rendering)
    sbuf.sputc(0);
    // Has death location (since disabled, death dimension name and death
    // location fields are not present)
    sbuf.sputc(0);
    // Portal cooldown in ticks
    sbuf.sputc(0);
    // Sea level
    sbuf.sputc(63);
    // Enforce secure chat
    sbuf.sputc(0);
}

asio::awaitable<void> graceful_disconnect(Session &session, std::string_view sv)
{
    session.get_streambuf().consume(session.get_streambuf().size());

    session.get_streambuf().sputc(0x20); // id Disconnect (play)
    NBTBuilder(std::ostreambuf_iterator(&session.get_streambuf()))
        << nbtbuilderdefinitions::String << sv;

    sys::error_code ec = co_await packetops::flush_packet(session);
    if (ec)
        std::println(
            "\tCan't send disconnect message to the client (error code: {}; reason: {})",
            ec, sv);
    else
        std::println("\tClient was disconnected with reason: {}", sv);

    session.get_socket().shutdown(asio::ip::tcp::socket::shutdown_both);
    session.get_socket().close();
}

asio::awaitable<void> statecoroutines::play(
    boost::intrusive_ptr<Session> session, std::string &&player_name,
    std::array<uint8_t, 16> &&player_uuid)
{
    std::println("\tstate_play");
    sys::error_code ec { };

    PacketRouter packet_router { *session };
    packet_router.begin_receiving();

    KeepAlive keep_alive { session, packet_router };
    asio::co_spawn(
        session->get_io(), keep_alive.keepalive_loop(), asio::detached);

    // Login (world state essentially)
    put_login_packet(session->get_streambuf());
    ec = co_await packetops::flush_packet(*session);
    if (ec)
        co_return co_await disconnect::play(
            *session, disconnect::fmt_desync(ec, "Login the packet"));

    // Synchronise Player Position
    int32_t teleport_id = g_i32_dist(g_rng);

    session->get_streambuf().sputc(0x48); // packet id
    streambufops::write_v32(session->get_streambuf(), teleport_id);
    // position, velocity
    for (double value : { 0.0, 80.0, 0.0, 0.0, 0.0, 0.0 })
        streambufops::write_real(session->get_streambuf(), value);
    // yaw, pitch
    for (float value : { 0.f, 0.f }) // looking towards positive Z
        streambufops::write_real(session->get_streambuf(), value);
    TeleportFlags::IntT teleport_flags { 0 };
    streambufops::write_integer(session->get_streambuf(), teleport_flags);

    ec = co_await packetops::flush_packet(*session);
    if (ec)
        co_return co_await disconnect::play(*session,
            disconnect::fmt_desync(ec, "Synchronise Player Position"));

    // Await for Confirm Teleportation
    {
        PacketRouter::PacketChannel channel { session->get_io() };
        auto sub = packet_router.subscribe(channel, 0x0);

        co_await channel.async_receive(asio::redirect_error(ec));
        if (ec)
            co_return co_await disconnect::play(
                *session, disconnect::fmt_desync(ec, "Confirm Teleportation"));

        int32_t got_teleport_id
            = streambufops::read_v32(session->get_streambuf(), ec);
        if (ec)
            co_return co_await disconnect::play(
                *session, disconnect::fmt_desync(ec, "Confirm Teleportation"));

        if (got_teleport_id != teleport_id)
            co_return co_await disconnect::play(*session,
                disconnect::fmt_desync(MCProtocolError::CorrelationIDMismatch,
                    "Confirm Teleportation"));

        if (session->get_streambuf().size() > 0)
            co_return co_await disconnect::play(*session,
                disconnect::fmt_desync(MCProtocolError::ExcessPacketData,
                    "Confirm Teleportation"));
    }

    // send Game Event 'Start waiting for level chunks'
    session->get_streambuf().sputc(0x26); // packet id
    session->get_streambuf().sputc(13); // event id
    streambufops::write_real(session->get_streambuf(), 0.f);

    ec = co_await packetops::flush_packet(*session);
    if (ec)
        co_return co_await disconnect::play(
            *session, disconnect::fmt_desync(ec, "Game Event"));

    // send Set Center Chunk
    session->get_streambuf().sputc(0x5E); // packet id
    streambufops::write_v32(session->get_streambuf(), 0); // X
    streambufops::write_v32(session->get_streambuf(), 0); // Z

    ec = co_await packetops::flush_packet(*session);
    if (ec)
        co_return co_await disconnect::play(
            *session, disconnect::fmt_desync(ec, "Set Center Chunk"));

    co_await asio::steady_timer(session->get_io(), std::chrono::seconds(120))
        .async_wait(asio::redirect_error(ec));
    if (ec)
        co_return co_await disconnect::play(
            *session, disconnect::fmt_desync(ec, "timer"));

    co_await disconnect::play(*session,
        "Gameplay not implemented yet."
        " But we could hold you in an empty world for 2 minutes."
        "\nPowered by ActualKlasterKraft");
}
