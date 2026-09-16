module;
#include <boost/asio.hpp>
#include <boost/system.hpp>
#include <exception>
#include <functional>
#include <print>
#include <stdexcept>
export module actualklasterkraft.packetrouter;

import actualklasterkraft.packetops;
import actualklasterkraft.protocolprimitives;
import actualklasterkraft.templates;
import actualklasterkraft.transport;

namespace asio = boost::asio;
namespace sys = boost::system;
using namespace protocolprimitives;
using asio::ip::tcp;

/******************************************************************************/

export class PacketSubscription
{
public:
    using Signature = void(sys::error_code, uint32_t);
    using MOF = std::move_only_function<Signature>;

public:
    PacketSubscription(const PacketSubscription &) = delete;
    PacketSubscription &operator=(const PacketSubscription &) = delete;

    PacketSubscription(PacketSubscription &&other) noexcept
    {
        m_table_entry = other.m_table_entry;
        other.m_table_entry = nullptr;
    }

    PacketSubscription &operator=(PacketSubscription &&other) noexcept
    {
        if (&other == this)
            return *this;

        if (m_table_entry)
            *m_table_entry = nullptr;

        m_table_entry = other.m_table_entry;
        other.m_table_entry = nullptr;
        return *this;
    }

    ~PacketSubscription()
    {
        if (m_table_entry)
            *m_table_entry = nullptr;
    }

    void reset() noexcept
    {
        *m_table_entry = nullptr;
        m_table_entry = nullptr;
    }

private:
    friend class PacketRouter;

    PacketSubscription(MOF *entry)
        : m_table_entry(entry)
    {
    }

private:
    MOF *m_table_entry { };
};

/******************************************************************************/

export class PacketRouter
{
public:
    using OnPacketSignature = PacketSubscription::Signature;
    using OnErrorSignature = void(sys::error_code);
    static constexpr uint32_t MaxPacketID = 128;

public:
    PacketRouter(Transport &transport, asio::streambuf &sb,
        asio::any_completion_handler<OnErrorSignature> &&on_error)
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

                if (m_callbacks[packet_id] == nullptr)
                {
                    std::println(
                        "PacketRouter::begin_receiving: received packet with ID 0x{:02X} without any subscribers",
                        packet_id);

                    m_streambuf.consume(m_streambuf.size());
                }
                else
                {
                    m_callbacks[packet_id](sys::error_code(), packet_id);
                }

                begin_receiving();
            });
    }

    PacketSubscription subscribe(uint32_t packet_id, auto &&functor_cb)
    {
        if (packet_id >= MaxPacketID)
            throw std::logic_error(
                "PacketRouter::subscribe: packet_id not in valid range");

        if (m_callbacks[packet_id] != nullptr)
            throw std::runtime_error(
                "PacketRouter::subscribe: packet_id already taken");

        m_callbacks[packet_id] = std::forward<decltype(functor_cb)>(functor_cb);
        return PacketSubscription(&m_callbacks[packet_id]);
    }

private:
    Transport &m_transport;
    asio::streambuf &m_streambuf;
    asio::any_completion_handler<OnErrorSignature> m_on_error;
    std::array<PacketSubscription::MOF, MaxPacketID> m_callbacks { };
};
