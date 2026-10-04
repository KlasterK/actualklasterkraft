module;
#include <boost/asio.hpp>
#include <exception>
#include <functional>
#include <memory>
#include <optional>
#include <print>
#include <stdexcept>
export module actualklasterkraft.net.play.packetrouter;

import actualklasterkraft.generic.errc;
import actualklasterkraft.generic.templates;
import actualklasterkraft.data.protocolprimitives;
import actualklasterkraft.net.base.packetops;
import actualklasterkraft.net.base.transport;

namespace asio = boost::asio;
namespace sys = boost::system;
using namespace protocolprimitives;
using asio::ip::tcp;

template <typename... Ts>
using MoveOnlyOrOldFunction =
#ifdef __cpp_lib_move_only_function
    std::move_only_function<Ts...>;
#else
    std::function<Ts...>;
#endif

export class PacketSubscription
{
public:
    using Signature = void(sys::error_code, uint32_t);
    using MOF = MoveOnlyOrOldFunction<Signature>;

public:
    PacketSubscription(const PacketSubscription &) = delete;
    PacketSubscription &operator=(const PacketSubscription &) = delete;

    // Subscription owns its table entry until reset() or move.
    // The entry (inside PacketRouter) MUST outlive the subscription.
    PacketSubscription(PacketSubscription &&other) noexcept
        : m_entry(other.m_entry)
        , m_owns(other.m_owns)
    {
        other.m_entry = nullptr;
        other.m_owns = false;
    }

    PacketSubscription &operator=(PacketSubscription &&other) noexcept
    {
        if (&other == this)
            return *this;

        reset();
        m_entry = other.m_entry;
        m_owns = other.m_owns;
        other.m_entry = nullptr;
        other.m_owns = false;

        return *this;
    }

    ~PacketSubscription() noexcept { reset(); }

    void reset() noexcept
    {
        if (m_owns && m_entry)
        {
            // Clear our slot so a later cancel_all() won't invoke
            // a dangling lambda (this was the crash on disconnect:
            // the Confirm-Teleportation handler outlived its stack frame).
            *m_entry = nullptr;
        }
        m_entry = nullptr;
        m_owns = false;
    }

private:
    friend class PacketRouter;

    explicit PacketSubscription(MOF *entry) noexcept
        : m_entry(entry)
        , m_owns(true)
    {
    }

private:
    MOF *m_entry { nullptr };
    bool m_owns { false };
};

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

    // `lifetime` keeps the session (which owns this router, the transport
    // and the streambuf) alive until the pending read finishes.
    // Without it the completion handler would dereference a destroyed
    // router after play() returns -> use-after-free crash on disconnect.
    void begin_receiving(std::shared_ptr<void> lifetime = { })
    {
        asio::co_spawn(m_transport.socket.get_executor(),
            packetops::get(m_transport, m_streambuf),
            [this, lifetime](std::exception_ptr exc_ptr, sys::error_code ec)
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

                begin_receiving(std::move(lifetime));
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

    void unsubscribe(uint32_t packet_id) noexcept
    {
        if (packet_id < MaxPacketID)
            m_callbacks[packet_id] = nullptr;
    }

    // Sticky receive-loop error. begin_receiving() terminates on the first
    // socket error; any task subscribing AFTER that would otherwise wait
    // forever (this hung play() when the client disconnected during init:
    // push_posrot_loop's channel never got a message because cancel_all had
    // already run before it subscribed). Late subscribers must bail out.
    bool dead() const noexcept { return m_sticky_error.has_value(); }
    sys::error_code sticky_error() const noexcept
    {
        return m_sticky_error.value_or(sys::error_code { });
    }

private:
    void cancel_all(sys::error_code ec)
    {
        m_sticky_error = ec;
        // Swap out callbacks before invoking: handlers may subscribe/
        // unsubscribe re-entrantly, and the router may be under teardown.
        // Skipping nulls also avoids invoking stale handlers.
        auto callbacks = std::move(m_callbacks);
        for (uint32_t packet_id = 0; packet_id < MaxPacketID; ++packet_id)
            if (callbacks[packet_id])
                callbacks[packet_id](ec, packet_id);
    }

private:
    Transport &m_transport;
    asio::streambuf &m_streambuf;
    std::array<PacketSubscription::MOF, MaxPacketID> m_callbacks { };
    std::optional<sys::error_code> m_sticky_error { };
};
