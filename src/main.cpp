#include <boost/asio.hpp>
#include <boost/asio/completion_condition.hpp>
#include <boost/asio/io_context.hpp>
#include <boost/asio/read.hpp>
#include <boost/asio/redirect_error.hpp>
#include <boost/asio/use_awaitable.hpp>
#include <boost/smart_ptr/intrusive_ptr.hpp>
#include <boost/smart_ptr/intrusive_ref_counter.hpp>
#include <boost/system/detail/error_code.hpp>
#include <exception>
#include <iostream>
#include <print>
#include <stdexcept>

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

    enum class State
    {
        Handshake,
    };

    State m_state { State::Handshake };

public:
    Session(asio::io_context &io, tcp::socket &&sock)
        : m_io(io)
        , m_sock(std::move(sock))
    {
    }

    /// Awaits for a packet and reads it into the streambuf (without Length).
    ///
    /// @returns false if EOF
    asio::awaitable<bool> await_for_packet() noexcept
    {
        char byte { };
        int32_t packet_size { };
        unsigned position { };
        sys::error_code err { };

        for (;;)
        {
            co_await asio::async_read(m_sock, asio::buffer(&byte, 1),
                asio::transfer_exactly(1), asio::redirect_error(err));
            if (err == asio::error::eof)
                co_return false;

            packet_size |= (byte & 0b01111111) << position;
            if ((byte & 0b10000000) == 0)
                break;

            position += 7;
            if (position > 32)
                throw std::runtime_error(
                    "Session::await_for_packet: VarInt is bigger than 5 bytes");
        }

        co_await asio::async_read(m_sock, m_streambuf,
            asio::transfer_exactly(packet_size), asio::redirect_error(err));
        if (err == asio::error::eof)
            co_return false;

        co_return true;
    }

    void next_packet()
    {
        asio::co_spawn(m_io, await_for_packet(),
            [self = boost::intrusive_ptr(this)](
                std::exception_ptr e_ptr, bool is_ok)
            {
                if (e_ptr)
                {
                    std::rethrow_exception(e_ptr);
                }

                if (is_ok)
                {
                    for (;;)
                    {
                        int byte = self->m_streambuf.sbumpc();
                        if (byte < 0)
                            break;

                        std::print("{:02x} ", byte);
                    }
                    self->m_streambuf.consume(self->m_streambuf.size());
                    std::println();

                    self->next_packet();
                }
                else
                {
                    std::cout << "Connection from "
                              << self->m_sock.remote_endpoint()
                              << " was closed with an EOF" << std::endl;
                }
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
            [&](const sys::error_code &err)
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
