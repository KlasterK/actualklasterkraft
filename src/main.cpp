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

    asio::awaitable<sys::error_code> state_handshake()
    {
        std::println("\tstate_handshake");
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
