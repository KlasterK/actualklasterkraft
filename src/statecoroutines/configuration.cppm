module;
#include <boost/asio.hpp>
#include <boost/intrusive_ptr.hpp>
#include <boost/smart_ptr/intrusive_ref_counter.hpp>
#include <boost/uuid.hpp>
#include <openssl/md5.h>
#include <print>
export module actualklasterkraft.statecoroutines.configuration;

import actualklasterkraft.errc;
import actualklasterkraft.packetops;
import actualklasterkraft.prebuiltconfiguration;
import actualklasterkraft.session;
import actualklasterkraft.streambufops;

namespace asio = boost::asio;
namespace sys = boost::system;

export namespace statecoroutines
{
    asio::awaitable<sys::error_code> configuration(
        boost::intrusive_ptr<Session> session, std::string &&player_name,
        std::array<uint8_t, 16> &&player_uuid)
    {
        std::println("\tstate_configuration");

        // Clientbound Known Packets
        session->get_streambuf().sputn(
            reinterpret_cast<const char *>(
                PrebuiltClientboundKnownPackets.data()),
            PrebuiltClientboundKnownPackets.size());
        auto ec = co_await packetops::flush_packet(*session);
        if (ec)
            co_return ec;

        // Registry Data (multiple)
        // PrebuiltRegistryData is a tuple of arrays

        // Accumulate error codes and do nothing if an error has present in a
        // previous call
        sys::error_code accumulated_ec { };
        auto send_pkt = [&](const auto &array) -> asio::awaitable<void>
        {
            if (accumulated_ec)
                co_return;
            session->get_streambuf().sputn(
                reinterpret_cast<const char *>(array.data()), array.size());
            accumulated_ec = co_await packetops::flush_packet(*session);
        };

        // Apply the lambda for each array in PrebuiltRegistryData
        co_await std::apply([&](auto &&...arrays) -> asio::awaitable<void>
            { (co_await send_pkt(arrays), ...); }, PrebuiltRegistryData);

        if (accumulated_ec)
            co_return accumulated_ec;

        // Update Tags
        session->get_streambuf().sputn(
            reinterpret_cast<const char *>(PrebuiltUpdateTags.data()),
            PrebuiltUpdateTags.size());
        ec = co_await packetops::flush_packet(*session);
        if (ec)
            co_return ec;

        // Finish Configuration (no fields)
        session->get_streambuf().sputc(0x03);
        ec = co_await packetops::flush_packet(*session);
        if (ec)
            co_return ec;

        // Ignore any packets until Acknowledge Finish Configuration
        for (int i { }; i < 2; ++i)
        {
            auto packet_result = co_await packetops::await_for_packet(*session);
            if (!packet_result)
                co_return packet_result.error();

            if (session->get_streambuf().sbumpc()
                == 0x03) // Acknowledge Finish Configuration
            {
                // No fields
                if (session->get_streambuf().in_avail() != 0)
                    co_return MCProtocolError::ExcessPacketData;

                session->get_streambuf().consume(1);
                break;
            }

            session->get_streambuf().consume(session->get_streambuf().size());
        }

        asio::steady_timer timer(session->get_io(), std::chrono::seconds(10));
        co_await timer.async_wait();

        co_return sys::error_code { };
    }
}
