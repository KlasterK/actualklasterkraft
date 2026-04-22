#include "errc.hpp"
#include <array>
#include <boost/asio.hpp>
#include <boost/smart_ptr.hpp>
#include <boost/smart_ptr/intrusive_ref_counter.hpp>
#include <cstdint>
#include <exception>
#include <expected>
#include <functional>
#include <iostream>
#include <optional>
#include <print>
#include <sstream>

namespace asio = boost::asio;
namespace sys = boost::system;
using tcp = boost::asio::ip::tcp;

class Session
    : public boost::intrusive_ref_counter<Session, boost::thread_unsafe_counter>
{
private:
    asio::io_context &m_io;
    tcp::socket m_sock;
    asio::streambuf m_streambuf;

    std::string m_remote_endpoint_name;
    asio::awaitable<sys::error_code> (Session::*m_state_cb)()
        = &Session::state_handshake;

private:
    void print_streambuf()
    {
        for (;;)
        {
            int byte = m_streambuf.sbumpc();
            if (byte < 0)
                break;

            std::print("{:02x} ", byte);
        }
        m_streambuf.consume(m_streambuf.size());
        std::println();
    }

    /// Awaits for a packet and reads it into the streambuf
    /// (without Packet Length).
    ///
    /// @returns Packet size or error code.
    asio::awaitable<std::expected<size_t, sys::error_code>> await_for_packet()
    {
        char byte { };
        int32_t packet_size { };
        unsigned position { };
        sys::error_code ec { };

        for (;;)
        {
            co_await asio::async_read(m_sock, asio::buffer(&byte, 1),
                asio::transfer_exactly(1), asio::redirect_error(ec));
            if (ec)
                co_return std::unexpected(ec);

            packet_size |= (byte & 0b01111111) << position;
            if ((byte & 0b10000000) == 0)
                break;

            position += 7;
            if (position > 32)
                co_return std::unexpected(MCProtocolError::VarIntTooBig);
        }

        co_await asio::async_read(m_sock, m_streambuf,
            asio::transfer_exactly(packet_size), asio::redirect_error(ec));

        if (ec)
            co_return std::unexpected(ec);

        co_return packet_size;
    }

    std::expected<int32_t, sys::error_code> read_vari32()
    {
        int byte { };
        int32_t value { };
        unsigned position { };

        for (;;)
        {
            byte = m_streambuf.sbumpc();
            if (byte < 0)
                return std::unexpected(asio::error::eof);

            value |= (byte & 0x7F);
            if ((byte & 0x80) == 0)
                break;

            position += 7;
            if (position > 32)
                return std::unexpected(MCProtocolError::VarIntTooBig);
        }
        return value;
    }

    template <std::integral T> std::optional<T> read_integer()
    {
        T value { };
        if (m_streambuf.sgetn(reinterpret_cast<char *>(&value), sizeof(T))
            < sizeof(T))
            return std::nullopt;

        return value;
    }

    asio::awaitable<sys::error_code> flush_packet()
    {
        std::array<uint8_t, 5> size_buf;
        auto size_end = size_buf.begin();

        for (uint32_t value = m_streambuf.size();;)
        {
            if ((value & ~0x7F) == 0)
            {
                *size_end++ = value & 0xFF;
                break;
            }
            *size_end++ = (value & 0x7F) | 0x80;
            value >>= 7u;
        }

        auto bufs = std::to_array<asio::const_buffer>(
            { { size_buf.begin(),
                  static_cast<size_t>(size_end - size_buf.begin()) },
                m_streambuf.data() });

        auto [ec, _] = co_await asio::async_write(m_sock, bufs, asio::as_tuple);
        m_streambuf.consume(m_streambuf.size());

        co_return ec;
    }

    void write_vari32(int32_t value)
    {
        for (;;)
        {
            if ((value & ~0x7F) == 0)
            {
                m_streambuf.sputc(value & 0xFF);
                return;
            }

            m_streambuf.sputc((value & 0x7F) | 0x80);
            value = int32_t(uint32_t(value) >> 7u);
        }
    }

    template <std::integral T> void write_integer(T value)
    {
        m_streambuf.sputn(reinterpret_cast<char *>(&value), sizeof(T));
    }

    asio::awaitable<sys::error_code> state_handshake()
    {
        std::println("\tstate_handshake");

        auto packet_result = co_await await_for_packet();
        if (!packet_result)
        {
            m_sock.close();
            co_return sys::error_code { };
        };

        // Packet ID
        if (m_streambuf.sbumpc() != 0x00)
        {
            m_sock.close();
            co_return sys::error_code { };
        };

        // Protocol Version
        auto vari32_result = read_vari32();
        if (!vari32_result)
        {
            m_sock.close();
            co_return sys::error_code { };
        };
        // Don't check version for now
        std::println("\tProtocol Version: {}", *vari32_result);

        // Server Address
        vari32_result = read_vari32();
        if (!vari32_result)
        {
            m_sock.close();
            co_return sys::error_code { };
        };

        {
            // Don't use this string, skip bytes. But can't move from streambuf
            // without reading, use a temp buffer for it.
            std::string dummy(*vari32_result, '\0');
            m_streambuf.sgetn(dummy.data(), *vari32_result);
        }

        // Server Port, u16, won't use
        if (!read_integer<uint16_t>().has_value())
        {
            m_sock.close();
            co_return sys::error_code { };
        };

        // Next State
        vari32_result = read_vari32();
        if (!vari32_result || *vari32_result != 1) // Status
        {
            m_sock.close();
            co_return sys::error_code { };
        };

        m_state_cb = &Session::state_status;

        if (m_streambuf.in_avail() != 0)
        {
            m_sock.close();
            co_return sys::error_code { };
        };

        m_streambuf.consume(m_streambuf.size());
        co_return sys::error_code { };
    }

    asio::awaitable<sys::error_code> state_status()
    {
        std::println("\tstate_status");

        // Getting packet Status Request
        auto packet_result = co_await await_for_packet();
        if (!packet_result)
            co_return packet_result.error();

        // Packet ID
        if (m_streambuf.sbumpc() != 0x00)
            co_return MCProtocolError::UnexpectedPacketID;

        if (m_streambuf.in_avail() != 0)
            co_return MCProtocolError::ExcessPacketData;
        m_streambuf.consume(m_streambuf.size());

        static constexpr std::string_view ExampleResponse = R"({
            "version": {
                "name": "26.1.2",
                "protocol": 775
            },
            "players": {
                "max": 20,
                "online": 1,
                "sample": []
            },
            "description": {
                "text": "Hello, world!"
            },
            "enforcesSecureChat": false
        })";

        m_streambuf.sputc(0x00); // Status Response
        write_vari32(ExampleResponse.size());
        m_streambuf.sputn(ExampleResponse.data(), ExampleResponse.size());

        auto ec = co_await flush_packet();
        if (ec)
            co_return ec;

        // Getting packet Ping Request
        packet_result = co_await await_for_packet();
        if (!packet_result)
            co_return packet_result.error();

        // Packet ID
        if (m_streambuf.sbumpc() != 0x01)
            co_return MCProtocolError::UnexpectedPacketID;

        auto payload_opt = read_integer<uint64_t>();
        if (!payload_opt)
            co_return MCProtocolError::UnsufficientPacketData;

        if (m_streambuf.in_avail() != 0)
            co_return MCProtocolError::ExcessPacketData;
        m_streambuf.consume(m_streambuf.size());

        // Pong Response
        m_streambuf.sputc(0x01); // ID
        write_integer<uint64_t>(*payload_opt);

        ec = co_await flush_packet();
        if (ec)
            co_return ec;

        m_sock.close();
        co_return sys::error_code { };
    }

public:
    Session(asio::io_context &io, tcp::socket &&sock)
        : m_io(io)
        , m_sock(std::move(sock))
    {
        std::ostringstream oss;
        oss << m_sock.remote_endpoint();
        m_remote_endpoint_name = std::move(oss.str());
    }

    void next_packet()
    {
        asio::co_spawn(m_io, std::invoke(m_state_cb, this),
            [self = boost::intrusive_ptr(this)](
                std::exception_ptr e_ptr, sys::error_code ec)
            {
                if (e_ptr)
                    std::rethrow_exception(e_ptr);

                if (ec)
                {
                    std::println("Connection from {} caused an error: {}",
                        self->m_remote_endpoint_name, ec.what());
                    self->m_sock.close();
                    return;
                }

                if (!self->m_sock.is_open())
                {
                    std::println("Connection {} was successfully closed",
                        self->m_remote_endpoint_name);
                    return;
                }

                self->next_packet();
            });
    }
};

class Acceptor
{
private:
    asio::io_context &m_io;
    tcp::acceptor m_acceptor;
    std::optional<tcp::socket> m_opt_sock { };

public:
    Acceptor(asio::io_context &io, uint16_t port)
        : m_io(io)
        , m_acceptor(io, tcp::endpoint(tcp::v4(), port))
    {
    }

    void accept()
    {
        m_opt_sock.emplace(m_io);
        m_acceptor.async_accept(*m_opt_sock,
            [this](const sys::error_code &err)
            {
                std::cout << "New connection from "
                          << m_opt_sock->remote_endpoint() << std::endl;

                boost::intrusive_ptr session { new Session {
                    m_io, std::move(*m_opt_sock) } };
                session->next_packet();
                accept();
            });
    }
};

int main()
{
    asio::io_context io;
    Acceptor acceptor(io, 25565);
    acceptor.accept();
    io.run();
    return 0;
}
