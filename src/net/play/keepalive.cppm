module;
#include <boost/asio.hpp>
#include <boost/asio/experimental/awaitable_operators.hpp>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <random>
export module actualklasterkraft.net.play.keepalive;

import actualklasterkraft.generic.errc;
import actualklasterkraft.generic.pubsub;
import actualklasterkraft.data.protocolprimitives;
import actualklasterkraft.net.base.packetops;
import actualklasterkraft.net.base.transport;
import actualklasterkraft.net.play.finalisation;

namespace asio = boost::asio;
namespace sys = boost::system;
using namespace asio::experimental::awaitable_operators;
using namespace std::literals;
using namespace protocolprimitives;
constexpr auto AwTuple = asio::as_tuple(asio::use_awaitable);

static std::minstd_rand g_rng { std::random_device { }() };

export class KeepAlive
{
public:
    static constexpr uint32_t ServerboundPacketID = 0x1C;
    static constexpr uint32_t ClientboundPacketID = 0x2C;

public:
    KeepAlive(Transport &transport)
        : m_transport(transport)
    {
    }

    std::tuple<sys::error_code, std::string_view> on_serverbound_packet(
        std::span<const uint8_t> data)
    {
        auto [payload, it, ec]
            = read_number<uint64_t>(data.begin(), data.end());
        if (ec)
            return { ec, "Serverbound Keep Alive" };
        if (it != data.end())
            return { MCProtocolError::ExcessPacketData,
                "Serverbound Keep Alive" };

        for (auto &active_payload : m_active_payloads)
        {
            if (active_payload == payload)
            {
                active_payload = 0;
                m_timeout_counter = 0;
                return { };
            }
        }

        return { MCProtocolError::CorrelationIDMismatch,
            "Serverbound Keep Alive" };
    }

    asio::awaitable<void> detached_clientbound_loop(FinalisationSignal &fin)
    {
        asio::steady_timer send_timer(m_transport.socket.get_executor());

        for (;;)
        {
            send_timer.expires_after(1s);
            auto wrapped_ec = co_await awaitable_or_fin(
                fin, send_timer.async_wait(AwTuple));
            if (!wrapped_ec)
                co_return;
            if (*wrapped_ec)
                co_return fin.emit(*wrapped_ec, "keep alive timer");

            if (m_timeout_counter++ > 30)
                co_return fin.emit(MCGameError::Timeout, "");

            std::array<uint8_t, 9> buf;
            buf[0] = ClientboundPacketID;
            uint64_t payload
                = std::uniform_int_distribution<uint64_t> { 1 }(g_rng);
            write_number<uint64_t>(buf.begin() + 1, payload);

            wrapped_ec = co_await awaitable_or_fin(
                fin, packetops::put(m_transport, asio::buffer(buf)));
            if (!wrapped_ec)
            {
                co_return;
            }
            if (*wrapped_ec)
            {
                fin.emit(*wrapped_ec, "Clientbound Keep Alive");
                co_return;
            }

            bool is_payload_stored { };
            for (uint64_t &active_payload : m_active_payloads)
            {
                if (active_payload == 0)
                {
                    active_payload = payload;
                    is_payload_stored = true;
                    break;
                }
            }

            if (!is_payload_stored)
            {
                auto dist = std::uniform_int_distribution { 0zu,
                    m_active_payloads.size() - 1 };
                m_active_payloads[dist(g_rng)] = payload;
            }
        }
    }

private:
    Transport &m_transport;
    // 0 payload is an empty slot
    std::array<uint64_t, 10> m_active_payloads { };
    int m_timeout_counter { };
};
