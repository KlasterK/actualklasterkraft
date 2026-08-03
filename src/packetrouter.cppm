module;
#include <boost/asio.hpp>
#include <boost/asio/experimental/channel.hpp>
#include <boost/intrusive_ptr.hpp>
#include <boost/smart_ptr/intrusive_ref_counter.hpp>
#include <boost/system.hpp>
#include <exception>
#include <print>
#include <stdexcept>
export module actualklasterkraft.packetrouter;

import actualklasterkraft.packetops;
import actualklasterkraft.session;
import actualklasterkraft.streambufops;

namespace asio = boost::asio;
namespace sys = boost::system;
namespace asiox = asio::experimental;

export class PacketRouter
{
public:
    using PacketChannel = asiox::channel<void(sys::error_code)>;
    static constexpr int32_t MaxPacketID = 128;

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
    PacketRouter(Session &session) noexcept
        : m_session(session)
    {
    }

    void begin_receiving()
    {
        asio::co_spawn(m_session.get_io(),
            packetops::await_for_packet(m_session),
            [this](std::exception_ptr exc_ptr, sys::error_code ec)
            {
                if (exc_ptr)
                    std::rethrow_exception(exc_ptr);

                // TODO: don't throw, handle using simple passing
                if (ec)
                    throw sys::system_error(ec);

                int32_t id
                    = streambufops::read_v32(m_session.get_streambuf(), ec);
                if (ec)
                    throw sys::system_error(ec);

                if (id < 0 || id >= MaxPacketID)
                    throw std::logic_error(
                        "PacketRouter::begin_receiving: received packet ID not in valid range");

                if (m_subscribers[id] == nullptr)
                {
                    std::println(
                        "\tPacketRouter::begin_receiving: received packet with ID {} without any subscribers",
                        id);

                    m_session.get_streambuf().consume(
                        m_session.get_streambuf().size());
                }
                else
                {
                    if (!m_subscribers[id]->try_send(sys::error_code { }))
                        throw std::runtime_error(std::format(
                            "PacketRouter::begin_receiving: sending to subscriber channel failed (packet ID {})",
                            id));
                }

                begin_receiving();
            });
    }

    [[nodiscard]] SubscriptionGuard subscribe(
        PacketChannel &channel, uint32_t packet_id)
    {
        if (packet_id < 0 || packet_id >= MaxPacketID)
            throw std::logic_error(
                "PacketRouter::subscribe: packet_id not in valid range");

        if (m_subscribers[packet_id] != nullptr)
            throw std::runtime_error(
                "PacketRouter::subscribe: packet_id already taken"); // TODO: implement multiple subscribing if it's possible

        m_subscribers[packet_id] = &channel;
        return { &m_subscribers[packet_id] };
    }

private:
    Session &m_session;
    std::array<PacketChannel *, MaxPacketID> m_subscribers { };
};
