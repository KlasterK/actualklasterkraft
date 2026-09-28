module;
#include <boost/asio.hpp>
#include <boost/asio/experimental/channel.hpp>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <random>
export module actualklasterkraft.net.play.keepalive;

import actualklasterkraft.generic.completiontokens;
import actualklasterkraft.generic.errc;
import actualklasterkraft.generic.templates;
import actualklasterkraft.data.protocolprimitives;
import actualklasterkraft.world.player;
import actualklasterkraft.world.posrot;
import actualklasterkraft.net.base.disconnecthelpers;
import actualklasterkraft.net.base.packetops;
import actualklasterkraft.net.base.transport;
import actualklasterkraft.net.play.packetrouter;

namespace asio = boost::asio;
namespace sys = boost::system;
using namespace std::literals;
using namespace protocolprimitives;
using ISI = std::istreambuf_iterator<char>;

static std::minstd_rand g_rng { std::random_device { }() };

constexpr int32_t ServerboundKeepAlivePacketID = 0x1C;
constexpr int32_t ClientboundKeepAlivePacketID = 0x2C;

export asio::awaitable<void> keepalive_loop(
    Transport &transport, asio::streambuf &sb, PacketRouter &packet_router)
{
    asio::steady_timer send_timer(transport.socket.get_executor());
    asio::experimental::channel<PacketRouter::OnPacketSignature>
        serverbound_keepalive_channel(transport.socket.get_executor());
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
                transport, disconnect::fmt_reason(ec, "Keep Alive timer"));

        if (timeout_counter++ > 2)
            co_return co_await disconnect::play(
                transport, "Timeout (powered by ActualKlasterKraft)");

        std::array<uint8_t, 9> buf;
        buf[0] = ClientboundKeepAlivePacketID;
        uint64_t payload = std::uniform_int_distribution<uint64_t> { }(g_rng);
        write_number<uint64_t>(buf.begin() + 1, payload);

        ec = co_await packetops::put(transport, asio::buffer(buf));
        if (is_normal_shutdown(ec))
            co_return;
        else if (ec)
            co_return co_await disconnect::play(transport,
                disconnect::fmt_reason(ec, "Clientbound Keep Alive"));

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
                disconnect::fmt_reason(ec, "Serverbound Keep Alive"));

        auto got_payload = InlineTie(TieReturn, std::ignore, ec)
            = read_number<uint64_t>(ISI(&sb), ISI());
        if (ec)
            co_return co_await disconnect::play(transport,
                disconnect::fmt_reason(ec, "Serverbound Keep Alive"));
        if (sb.size() > 0)
            co_return co_await disconnect::play(transport,
                disconnect::fmt_reason(MCProtocolError::ExcessPacketData,
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
            disconnect::fmt_reason(MCProtocolError::CorrelationIDMismatch,
                "Serverbound Keep Alive"));

    payload_matched:
        continue;
    }
}