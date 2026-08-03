module;
#include <boost/asio.hpp>
#include <boost/intrusive_ptr.hpp>
#include <boost/smart_ptr/intrusive_ref_counter.hpp>
#include <boost/uuid.hpp>
#include <new>
#include <openssl/md5.h>
#include <print>
export module actualklasterkraft.statecoroutines.configuration;

import actualklasterkraft.disconnecthelpers;
import actualklasterkraft.errc;
import actualklasterkraft.nbtbuilder;
import actualklasterkraft.packetops;
import actualklasterkraft.prebuiltconfiguration;
import actualklasterkraft.session;
import actualklasterkraft.formatters;
import actualklasterkraft.streambufops;
import actualklasterkraft.statecoroutines.play;

namespace asio = boost::asio;
namespace sys = boost::system;

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
                co_return co_await disconnect::configuration(*session,
                    disconnect::fmt_desync(
                        ec, "prebuilt Configuration state packets"));
            packet_it += packet_length;
        }

        // Finish Configuration (no fields)
        session->get_streambuf().sputc(0x03);
        ec = co_await packetops::flush_packet(*session);
        if (ec)
            co_return co_await disconnect::configuration(
                *session, disconnect::fmt_desync(ec, "Finish Configuration"));

        // Ignore any packets until Acknowledge Finish Configuration
        for (;;)
        {
            ec = co_await packetops::await_for_packet(*session);
            if (ec)
                co_return co_await disconnect::configuration(*session,
                    disconnect::fmt_desync(
                        ec, "Acknowledge Finish Configuration"));

            // Acknowledge Finish Configuration
            if (session->get_streambuf().sbumpc() == 0x03)
            {
                // No fields
                if (session->get_streambuf().size() > 0)
                    co_return co_await disconnect::configuration(*session,
                        disconnect::fmt_desync(
                            MCProtocolError::ExcessPacketData,
                            "Acknowledge Finish Configuration"));
                break;
            }
            session->get_streambuf().consume(session->get_streambuf().size());
        }

        asio::co_spawn(session->get_io(),
            statecoroutines::play(
                session, std::move(player_name), std::move(player_uuid)),
            [session](std::exception_ptr exc_ptr)
            { session->handle_coroutine_finished(exc_ptr); });
    }
}
