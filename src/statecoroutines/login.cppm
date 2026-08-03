module;
#include <boost/asio.hpp>
#include <boost/intrusive_ptr.hpp>
#include <boost/smart_ptr/intrusive_ref_counter.hpp>
#include <boost/system/detail/error_code.hpp>
#include <boost/uuid.hpp>
#include <coroutine>
#include <cstdint>
#include <openssl/md5.h>
#include <print>
#include <string_view>
export module actualklasterkraft.statecoroutines.login;

import actualklasterkraft.errc;
import actualklasterkraft.formatters;
import actualklasterkraft.packetops;
import actualklasterkraft.session;
import actualklasterkraft.statecoroutines.configuration;
import actualklasterkraft.streambufops;

namespace asio = boost::asio;
namespace sys = boost::system;

[[nodiscard]] std::array<uint8_t, 16> generate_java_uuid3(
    std::span<const uint8_t> data)
{
    std::array<uint8_t, 16> uuid;
    MD5(data.data(), data.size(), uuid.data());
    // Set version to 3
    uuid[6] = (uuid[6] & 0b00001111) | 0b00110000;
    // Set variant to 1
    uuid[8] = (uuid[8] & 0b00111111) | 0b10000000;
    return uuid;
}

asio::awaitable<void> login_disconnect(
    Session &session, std::string_view json_reason)
{
    session.get_streambuf().consume(session.get_streambuf().size());

    session.get_streambuf().sputc(0x00); // id Login Disconnect
    streambufops::write_string(session.get_streambuf(), json_reason);

    auto ec = co_await packetops::flush_packet(session);
    if (ec)
        std::println(
            "\tCan't send disconnect message to the client (error code: {}; reason: {})",
            ec, json_reason);
    else
        std::println("\tClient was disconnected with reason: {}", json_reason);

    session.get_socket().shutdown(asio::ip::tcp::socket::shutdown_both);
    session.get_socket().close();
}

export namespace statecoroutines
{
    asio::awaitable<void> login(
        boost::intrusive_ptr<Session> session, bool is_transfer)
    {
        (void)is_transfer;
        sys::error_code ec { };
        std::println("\tstate_login");

        ec = co_await packetops::await_for_packet(*session);
        if (ec)
            co_return co_await login_disconnect(*session,
                std::format(
                    "{{\"text\": \"Protocol Desync: could't get packet Login Start due to error: {}\"}}",
                    ec));

        if (session->get_streambuf().sbumpc() != 0x00) // Login Start
            co_return co_await login_disconnect(*session,
                "{\"text\": \"Protocol Desync: unexpected packet ID (should be Login Start)\"}");

        int32_t name_len = streambufops::read_v32(session->get_streambuf(), ec);
        if (ec)
            co_return co_await login_disconnect(*session,
                std::format(
                    "{{\"text\": \"Protocol Desync: error while parsing packet: {}\"}}",
                    ec));
        if (name_len < 1)
            co_return co_await login_disconnect(*session,
                "{\"text\":\"Your name can't be empty or negative size.\"}");
        if (name_len > 16)
            co_return co_await login_disconnect(*session,
                "{\"text\":\"Your name is longer than 16 characters.\"}");

        // Needed to generate offline player UUID
        constexpr std::string_view UUIDDomainPrefix = "OfflinePlayer:";
        // Allocate enough memory for prefix and name
        std::string prefixed_player_name(
            name_len + UUIDDomainPrefix.size(), '\0');
        // Copy prefix into the beginning
        std::ranges::copy(UUIDDomainPrefix, prefixed_player_name.begin());
        // View of the name only
        std::string_view player_name_view(
            prefixed_player_name.data() + UUIDDomainPrefix.size(), name_len);
        // Copy the name after the prefix
        session->get_streambuf().sgetn(
            prefixed_player_name.data() + UUIDDomainPrefix.size(), name_len);

        // Player UUID from the packet (ignored, server will assign a UUID itself)
        session->get_streambuf().consume(16);

        if (session->get_streambuf().size() > 0)
            co_return co_await login_disconnect(*session,
                "{\"text\": \"Protocol Desync: excess packet data\"}");

        // Generate UUID for the player
        auto player_uuid = generate_java_uuid3(
            { reinterpret_cast<const uint8_t *>(prefixed_player_name.data()),
                prefixed_player_name.size() });

        // Login Success
        session->get_streambuf().sputc(0x02);
        // UUID
        session->get_streambuf().sputn(
            reinterpret_cast<const char *>(player_uuid.data()),
            player_uuid.size());
        // Name
        streambufops::write_string(session->get_streambuf(), player_name_view);
        // Properties (none, length = 0)
        session->get_streambuf().sputc(0x00);

        ec = co_await packetops::flush_packet(*session);
        if (ec)
            co_return co_await login_disconnect(*session,
                std::format(
                    "{{\"text\": \"Protocol Desync: could't send packet due to error: {}\"}}",
                    ec));

        // Ignore any packets until Login Acknowledged
        for (;;)
        {
            ec = co_await packetops::await_for_packet(*session);
            if (ec)
                co_return co_await login_disconnect(*session,
                    std::format(
                        "{{\"text\": \"Protocol Desync: could't receive packet due to error: {}\"}}",
                        ec));

            if (session->get_streambuf().sbumpc() == 0x03) // Login Acknowledged
            {
                if (session->get_streambuf().size() > 0) // No fields
                    co_return co_await login_disconnect(*session,
                        "{\"text\": \"Protocol Desync: excess packet data\"}");
                break;
            }

            session->get_streambuf().consume(session->get_streambuf().size());
        }

        asio::co_spawn(session->get_io(),
            statecoroutines::configuration(
                session, std::string(player_name_view), std::move(player_uuid)),
            [session](std::exception_ptr exc_ptr)
            { session->handle_coroutine_finished(exc_ptr); });
    }
}
