module;
#include <boost/asio.hpp>
#include <boost/intrusive/link_mode.hpp>
#include <boost/intrusive/list.hpp>
#include <boost/intrusive/options.hpp>
#include <boost/intrusive/slist.hpp>
#include <boost/system.hpp>
#include <exception>
#include <functional>
#include <print>
#include <stdexcept>
export module actualklasterkraft.packetrouter;

import actualklasterkraft.errc;
import actualklasterkraft.packetops;
import actualklasterkraft.protocolprimitives;
import actualklasterkraft.templates;
import actualklasterkraft.transport;

namespace asio = boost::asio;
namespace sys = boost::system;
namespace bi = boost::intrusive;
using namespace protocolprimitives;
using asio::ip::tcp;

template <typename... Ts>
using MoveOnlyOrOldFunction =
#ifdef __cpp_lib_move_only_function
    std::move_only_function<Ts...>;
#else
    std::function<Ts...>;
#endif

/******************************************************************************/

export class PacketSubscription
{
public:
    using Signature = void(sys::error_code, uint32_t);
    using MOF = MoveOnlyOrOldFunction<Signature>;

public:
    PacketSubscription(const PacketSubscription &) = delete;
    PacketSubscription &operator=(const PacketSubscription &) = delete;

    PacketSubscription(PacketSubscription &&other) noexcept
        : m_table_entry(other.m_table_entry)
    {
        std::swap(m_hook, other.m_hook);
    }

    PacketSubscription &operator=(PacketSubscription &&other) noexcept
    {
        if (&other == this)
            return *this;

        reset();
        m_table_entry = other.m_table_entry;
        std::swap(m_hook, other.m_hook);

        return *this;
    }

    ~PacketSubscription() noexcept { reset(); }

    void reset() noexcept
    {
        if (m_hook.is_linked())
            m_table_entry.get() = nullptr;
        m_hook.unlink();
    }

private:
    friend class PacketRouter;

    PacketSubscription(MOF &entry) noexcept
        : m_table_entry(entry)
    {
    }

private:
    std::reference_wrapper<MOF> m_table_entry;
    bi::list_member_hook<bi::link_mode<bi::auto_unlink>> m_hook;
};

/******************************************************************************/

export class PacketRouter
{
public:
    using OnPacketSignature = PacketSubscription::Signature;
    using OnErrorSignature = void(sys::error_code);
    static constexpr uint32_t MaxPacketID = 128;

public:
    PacketRouter(Transport &transport, asio::streambuf &sb)
        : m_transport(transport)
        , m_streambuf(sb)
    {
    }

    PacketRouter(const PacketRouter &) = delete;
    PacketRouter(PacketRouter &&) = delete;
    PacketRouter &operator=(const PacketRouter &) = delete;
    PacketRouter &operator=(PacketRouter &&) = delete;

    void begin_receiving()
    {
        asio::co_spawn(m_transport.socket.get_executor(),
            packetops::get(m_transport, m_streambuf),
            [this](std::exception_ptr exc_ptr, sys::error_code ec)
            {
                if (exc_ptr)
                    std::rethrow_exception(exc_ptr);

                if (ec)
                    return cancel_all(ec);

                auto packet_id = InlineTie(TieReturn, std::ignore, ec)
                    = read_var<uint32_t>(
                        std::istreambuf_iterator<char>(&m_streambuf),
                        std::istreambuf_iterator<char>());
                if (ec)
                    return cancel_all(ec);

                if (packet_id >= MaxPacketID)
                    return cancel_all(MCProtocolError::UnexpectedPacketID);

                if (m_callbacks[packet_id] == nullptr)
                {
                    std::println(
                        "PacketRouter::begin_receiving: received packet with ID 0x{:02X} without any subscribers",
                        packet_id);

                    m_streambuf.consume(m_streambuf.size());
                }
                else
                {
                    m_callbacks[packet_id](sys::error_code { }, packet_id);
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
        return PacketSubscription(m_callbacks[packet_id]);
    }

private:
    void cancel_all(sys::error_code ec)
    {
        for (uint32_t packet_id = 0; packet_id < MaxPacketID; ++packet_id)
            if (m_callbacks[packet_id])
                m_callbacks[packet_id](ec, packet_id);
    }

private:
    Transport &m_transport;
    asio::streambuf &m_streambuf;
    bi::slist<PacketSubscription,
        bi::member_hook<PacketSubscription,
            decltype(PacketSubscription::m_hook), &PacketSubscription::m_hook>,
        bi::constant_time_size<false>>
        m_subscriptions;
    std::array<PacketSubscription::MOF, MaxPacketID> m_callbacks { };
};
