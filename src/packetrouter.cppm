module;
#include <boost/asio.hpp>
#include <boost/asio/experimental/channel.hpp>
#include <boost/system.hpp>
#include <exception>
#include <print>
#include <stdexcept>
export module actualklasterkraft.packetrouter;

import actualklasterkraft.packetops;
import actualklasterkraft.protocolprimitives;
import actualklasterkraft.templates;
import actualklasterkraft.transport;

namespace asio = boost::asio;
namespace sys = boost::system;
namespace asiox = asio::experimental;
using asio::ip::tcp;
using namespace protocolprimitives;

export class PacketRouter
{
public:
    using PacketChannel = asiox::channel<void(sys::error_code)>;
    static constexpr uint32_t MaxPacketID = 128;

    class SubscriptionGuard
    {
    public:
        ~SubscriptionGuard()
        {
            if (m_p)
                *m_p = nullptr;
        }

        SubscriptionGuard(const SubscriptionGuard &) = delete;
        SubscriptionGuard &operator=(const SubscriptionGuard &) = delete;

        SubscriptionGuard(SubscriptionGuard &&other) noexcept
        {
            m_p = other.m_p;
            other.m_p = nullptr;
        }

        SubscriptionGuard &operator=(SubscriptionGuard &&other) noexcept
        {
            if (&other == this)
                return *this;

            if (m_p)
                *m_p = nullptr;

            m_p = other.m_p;
            other.m_p = nullptr;
            return *this;
        }

        void release() noexcept
        {
            *m_p = nullptr;
            m_p = nullptr;
        }

    private:
        friend PacketRouter;

        SubscriptionGuard(PacketChannel **p) { m_p = p; }

        PacketChannel **m_p { };
    };

public:
    PacketRouter(Transport &transport, asio::streambuf &sb,
        asio::any_completion_handler<void(sys::error_code)> on_error)
        : m_transport(transport)
        , m_streambuf(sb)
        , m_on_error(std::move(on_error))
    {
    }

    void begin_receiving()
    {
        asio::co_spawn(m_transport.socket.get_executor(),
            packetops::get(m_transport, m_streambuf),
            [this](std::exception_ptr exc_ptr, sys::error_code ec)
            {
                if (exc_ptr)
                    std::rethrow_exception(exc_ptr);

                if (ec)
                    return m_on_error(ec);

                auto packet_id = InlineTie(TieReturn, std::ignore, ec)
                    = read_var<uint32_t>(
                        std::istreambuf_iterator<char>(&m_streambuf),
                        std::istreambuf_iterator<char>());
                if (ec)
                    return m_on_error(ec);

                if (packet_id >= MaxPacketID)
                    throw std::logic_error(
                        "PacketRouter::begin_receiving: received packet ID not in valid range");

                if (m_subscribers[packet_id] == nullptr)
                {
                    std::println(
                        "\tPacketRouter::begin_receiving: received packet with ID 0x{:02X} without any subscribers",
                        packet_id);

                    m_streambuf.consume(m_streambuf.size());
                }
                else
                {
                    if (!m_subscribers[packet_id]->try_send(
                            sys::error_code { }))
                        throw std::runtime_error(std::format(
                            "PacketRouter::begin_receiving: sending to subscriber channel failed (packet ID 0x{:02X})",
                            packet_id));
                }

                begin_receiving();
            });
    }

    [[nodiscard]] SubscriptionGuard subscribe(
        PacketChannel &channel, uint32_t packet_id)
    {
        if (packet_id >= MaxPacketID)
            throw std::logic_error(
                "PacketRouter::subscribe: packet_id not in valid range");

        if (m_subscribers[packet_id] != nullptr)
            throw std::runtime_error(
                "PacketRouter::subscribe: packet_id already taken"); // TODO: implement multiple subscribing if it's possible

        m_subscribers[packet_id] = &channel;
        return { &m_subscribers[packet_id] };
    }

private:
    Transport &m_transport;
    asio::streambuf &m_streambuf;
    asio::any_completion_handler<void(sys::error_code)> m_on_error;
    std::array<PacketChannel *, MaxPacketID> m_subscribers { };
};
