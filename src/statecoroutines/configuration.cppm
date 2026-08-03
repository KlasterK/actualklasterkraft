module;
#include <boost/asio.hpp>
#include <boost/intrusive_ptr.hpp>
#include <boost/smart_ptr/intrusive_ref_counter.hpp>
#include <boost/uuid.hpp>
#include <new>
#include <openssl/md5.h>
#include <print>
export module actualklasterkraft.statecoroutines.configuration;

import actualklasterkraft.errc;
import actualklasterkraft.nbtbuilder;
import actualklasterkraft.packetops;
import actualklasterkraft.prebuiltconfiguration;
import actualklasterkraft.session;
import actualklasterkraft.formatters;
import actualklasterkraft.streambufops;

namespace asio = boost::asio;
namespace sys = boost::system;

asio::awaitable<void> graceful_disconnect(Session &session, std::string_view sv)
{
    session.get_streambuf().consume(session.get_streambuf().size());

    session.get_streambuf().sputc(0x02); // id Disconnect (configuration)
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

export namespace statecoroutines
{
    asio::awaitable<void> configuration(boost::intrusive_ptr<Session> session,
        std::string &&player_name, std::array<uint8_t, 16> &&player_uuid)
    {
        sys::error_code ec { };
        std::println("\tstate_configuration");

        // For Configuration, we should synchronise our game data with client's game data.
        // We'll ignore serverbound packets for simplicity.

        auto packet_it = PrebuiltConfigurationStatePackets.data.begin();
        for (size_t packet_length : PrebuiltConfigurationStatePackets.lengths)
        {
            if (packet_length == 0)
                break;

            ec = co_await packetops::flush_packet(
                *session, asio::buffer(packet_it, packet_length));
            if (ec)
                co_return co_await graceful_disconnect(*session,
                    std::format(
                        "Protocol Desync: couldn't send packet due to error: {}",
                        ec));
            packet_it += packet_length;
        }

        // Finish Configuration (no fields)
        session->get_streambuf().sputc(0x03);
        ec = co_await packetops::flush_packet(*session);
        if (ec)
            co_return co_await graceful_disconnect(*session,
                std::format(
                    "Protocol Desync: couldn't send packet due to error: {}",
                    ec));

        // Ignore any packets until Acknowledge Finish Configuration
        for (;;)
        {
            ec = co_await packetops::await_for_packet(*session);
            if (ec)
                co_return co_await graceful_disconnect(*session,
                    std::format(
                        "Protocol Desync: could't get packet Login Start due to error: {}",
                        ec));

            // Acknowledge Finish Configuration
            if (session->get_streambuf().sbumpc() == 0x03)
            {
                // No fields
                if (session->get_streambuf().size() > 0)
                    co_return co_await graceful_disconnect(
                        *session, "Protocol Desync: excess packet data");
                break;
            }
            session->get_streambuf().consume(session->get_streambuf().size());
        }

        // We're in Play state now
    }
}
