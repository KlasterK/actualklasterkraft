module;
#include <boost/asio.hpp>
#include <boost/asio/detached.hpp>
#include <boost/system.hpp>
#include <cstdint>
#include <memory>
#include <openssl/err.h>
#include <openssl/evp.h>
#include <print>
#include <string_view>
export module actualklasterkraft.statecoroutines.login;

import actualklasterkraft.disconnecthelpers;
import actualklasterkraft.errc;
import actualklasterkraft.formatters;
import actualklasterkraft.packetops;
import actualklasterkraft.protocolprimitives;
import actualklasterkraft.templates;
import actualklasterkraft.transport;
import actualklasterkraft.statecoroutines.configuration;

using namespace std::literals;
using namespace protocolprimitives;
namespace asio = boost::asio;
using asio::ip::tcp;
using UniqueEVP_MD_CTX = std::unique_ptr<EVP_MD_CTX, void (&)(EVP_MD_CTX *)>;

auto openssl_exception(std::string_view caller, std::string_view callee)
{
    std::array<char, 256> err_buf;
    ERR_error_string_n(ERR_get_error(), err_buf.data(), err_buf.size());
    return std::runtime_error(std::format(
        "{}: {} failed with error: {}", caller, callee, err_buf.data()));
}

const auto OfflinePlayerMD5ContextTemplate = []
{
    UniqueEVP_MD_CTX ctx { EVP_MD_CTX_new(), EVP_MD_CTX_free };
    if (!ctx)
        throw openssl_exception(
            "OfflinePlayerMD5ContextTemplate (IIFE)", "EVP_MD_CTX_new");

    if (0 == EVP_DigestInit_ex2(ctx.get(), EVP_md5(), nullptr))
        throw openssl_exception(
            "OfflinePlayerMD5ContextTemplate (IIFE)", "EVP_DigestInit_ex2");

    auto prefix = "OfflinePlayer:"sv;
    if (0 == EVP_DigestUpdate(ctx.get(), prefix.data(), prefix.size()))
        throw openssl_exception(
            "OfflinePlayerMD5ContextTemplate (IIFE)", "EVP_DigestUpdate");

    return ctx;
}();

[[nodiscard]] std::array<uint8_t, 16> offline_player_uuid(std::string_view name)
{
    UniqueEVP_MD_CTX ctx {
        EVP_MD_CTX_dup(OfflinePlayerMD5ContextTemplate.get()), EVP_MD_CTX_free
    };
    if (!ctx)
        throw openssl_exception("generate_java_uuid3", "EVP_MD_CTX_dup");

    if (0 == EVP_DigestUpdate(ctx.get(), name.data(), name.size()))
        throw openssl_exception("generate_java_uuid3", "EVP_DigestUpdate");

    std::array<uint8_t, 16> uuid;
    if (0 == EVP_DigestFinal_ex(ctx.get(), uuid.data(), nullptr))
        throw openssl_exception("generate_java_uuid3", "EVP_DigestFinal");

    // Set version to 3
    uuid[6] = (uuid[6] & 0b00001111) | 0b00110000;
    // Set variant to 1
    uuid[8] = (uuid[8] & 0b00111111) | 0b10000000;
    return uuid;
}

export namespace statecoroutines
{
    asio::awaitable<void> login(Transport transport, bool is_transfer)
    {
        (void)is_transfer;

        std::array<uint8_t, 64> buf;
        auto it = buf.begin();

        auto [ec, packet_size]
            = co_await packetops::get(transport, asio::buffer(buf));
        if (ec)
            co_return co_await disconnect::login(
                transport, disconnect::fmt_desync(ec, "Login Start"));
        auto end = it + packet_size;

        if (*it++ != 0x00) // Login Start
            co_return co_await disconnect::login(transport,
                disconnect::fmt_desync(
                    MCProtocolError::UnexpectedPacketID, "Login Start"));

        auto name_len = InlineTie(TieReturn, it, ec)
            = read_var<uint32_t>(it, end);
        if (ec)
            co_return co_await disconnect::login(
                transport, disconnect::fmt_desync(ec, "Login Start"));
        if (name_len < 1)
            co_return co_await disconnect::login(
                transport, "Your name can't be empty");
        if (name_len > 16)
            co_return co_await disconnect::login(
                transport, "Your name is longer than 16 characters");

        std::string player_name(it, it + name_len);
        auto player_uuid = offline_player_uuid(player_name);

        // UUID comes next but we'll ignore serverbound UUID
        it += name_len + 16;
        if (end < it)
            co_return co_await disconnect::login(transport,
                disconnect::fmt_desync(
                    MCProtocolError::UnsufficientPacketData, "Login Start"));
        if (end > it)
            co_return co_await disconnect::login(transport,
                disconnect::fmt_desync(
                    MCProtocolError::ExcessPacketData, "Login Start"));

        // Login Success
        it = buf.begin();
        *it++ = 0x02;
        // UUID
        it = std::copy(player_uuid.begin(), player_uuid.end(), it);
        // Name
        it = write_string(it, player_name);
        // Properties (none)
        *it++ = 0;

        ec = co_await packetops::put(
            transport, asio::buffer(buf.data(), it - buf.begin()));
        if (ec)
            co_return co_await disconnect::login(
                transport, disconnect::fmt_desync(ec, "Login Success"));

        // Ignore any packets until Login Acknowledged
        for (;;)
        {
            std::tie(ec, packet_size)
                = co_await packetops::get(transport, asio::buffer(buf));
            if (ec)
                co_return co_await disconnect::login(transport,
                    disconnect::fmt_desync(ec, "Login Acknowledged"));
            if (packet_size < 0)
                co_return co_await disconnect::login(transport,
                    disconnect::fmt_desync(
                        MCProtocolError::UnsufficientPacketData,
                        "Login Acknowledged"));

            if (buf[0] == 0x03) // Login Acknowledged
            {
                if (packet_size > 1) // No fields
                    co_return co_await disconnect::login(transport,
                        disconnect::fmt_desync(
                            MCProtocolError::ExcessPacketData,
                            "Login Acknowledged"));
                break;
            }
        }

        std::println("Client {} is joining as {}",
            transport.remote_endpoint_copy, player_name);
        asio::co_spawn(transport.socket.get_executor(),
            statecoroutines::configuration(std::move(transport),
                std::move(player_name), std::move(player_uuid)),
            asio::detached);
    }
}
