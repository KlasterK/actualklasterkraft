module;
#include <boost/asio.hpp>
#include <boost/intrusive_ptr.hpp>
#include <boost/smart_ptr/intrusive_ref_counter.hpp>
#include <boost/uuid.hpp>
#include <coroutine>
#include <cstdint>
#include <openssl/md5.h>
#include <print>
#include <string_view>
export module actualklasterkraft.statecoroutines.login;

import actualklasterkraft.errc;
import actualklasterkraft.packetops;
import actualklasterkraft.session;
import actualklasterkraft.statecoroutines.configuration;
import actualklasterkraft.streambufops;

namespace asio = boost::asio;
namespace sys = boost::system;

std::array<uint8_t, 16> generate_java_uuid3(std::span<const uint8_t> data)
{
    std::array<uint8_t, 16> uuid;
    MD5(data.data(), data.size(), uuid.data());
    // Set version to 3
    uuid[6] = (uuid[6] & 0b00001111) | 0b00110000;
    // Set variant to 1
    uuid[8] = (uuid[8] & 0b00111111) | 0b10000000;
    return uuid;
}

asio::awaitable<sys::error_code> login_disconnect(
    Session &session, std::string_view json_reason)
{
    session.get_streambuf().consume(session.get_streambuf().size());
    session.get_streambuf().sputc(0x00); // Login Disconnect
    streambufops::write_vari32(session.get_streambuf(), json_reason.size());
    session.get_streambuf().sputn(json_reason.data(), json_reason.size());

    auto ec = co_await packetops::flush_packet(session);
    if (ec)
        co_return ec;

    session.get_socket().close();
    co_return sys::error_code { };
}

export namespace statecoroutines
{
    asio::awaitable<sys::error_code> login(
        boost::intrusive_ptr<Session> session, bool is_transfer)
    {
        (void)is_transfer;
        std::println("\tstate_login");

        auto packet_result = co_await packetops::await_for_packet(*session);
        if (!packet_result)
            co_return packet_result.error();

        if (session->get_streambuf().sbumpc() != 0x00) // Login Start
            co_return MCProtocolError::UnexpectedPacketID;

        auto vari32_result
            = streambufops::read_vari32(session->get_streambuf());
        if (!vari32_result)
            co_return vari32_result.error();

        if (*vari32_result > 16)
            co_return co_await login_disconnect(*session,
                "{\"text\":\"Your name is longer than 16 characters.\"}");

        std::string player_name(*vari32_result, '\0');
        session->get_streambuf().sgetn(player_name.data(), *vari32_result);

        // Player UUID (ignored, server will assign a UUID itself)
        {
            std::array<char, 16> dummy { };
            if (session->get_streambuf().sgetn(dummy.data(), dummy.size())
                != dummy.size())
                co_return MCProtocolError::UnsufficientPacketData;
        }

        if (session->get_streambuf().in_avail() != 0)
            co_return MCProtocolError::ExcessPacketData;
        session->get_streambuf().consume(session->get_streambuf().size());

        // Generate UUID for the player
        auto player_uuid = generate_java_uuid3(
            { reinterpret_cast<const uint8_t *>(player_name.data()),
                player_name.size() });

        // Login Success
        session->get_streambuf().sputc(0x02);
        // UUID
        session->get_streambuf().sputn(
            reinterpret_cast<const char *>(player_uuid.data()),
            player_uuid.size());
        // Name
        streambufops::write_vari32(
            session->get_streambuf(), player_name.size());
        session->get_streambuf().sputn(player_name.data(), player_name.size());
        // Properties (none, length = 0)
        session->get_streambuf().sputc(0x00);

        auto ec = co_await packetops::flush_packet(*session);
        if (ec)
            co_return ec;

        // Ignore any packets until Login Acknowledged
        for (;;)
        {
            packet_result = co_await packetops::await_for_packet(*session);
            if (!packet_result)
                co_return packet_result.error();

            if (session->get_streambuf().sbumpc() == 0x03) // Login Acknowledged
            {
                // No fields
                if (session->get_streambuf().in_avail() != 0)
                    co_return MCProtocolError::ExcessPacketData;

                session->get_streambuf().consume(1);
                break;
            }

            session->get_streambuf().consume(session->get_streambuf().size());
        }

        asio::co_spawn(session->get_io(),
            statecoroutines::configuration(
                session, std::move(player_name), std::move(player_uuid)),
            [session](std::exception_ptr exc_ptr, sys::error_code ec)
            { session->handle_coroutine_finished(exc_ptr, ec); });

        co_return sys::error_code { };
    }
}
