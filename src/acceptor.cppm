module;
#include <boost/asio.hpp>
#include <boost/intrusive_ptr.hpp>
#include <boost/smart_ptr/intrusive_ref_counter.hpp>
#include <print>
export module actualklasterkraft.acceptor;

import actualklasterkraft.formatters;
import actualklasterkraft.session;
import actualklasterkraft.statecoroutines.handshake;

namespace asio = boost::asio;
namespace sys = boost::system;
using asio::ip::tcp;

export class Acceptor
{
public:
    Acceptor(asio::io_context &io, uint16_t port)
        : m_io(io)
        , m_acceptor(io, tcp::endpoint(tcp::v4(), port))
    {
    }

    void start()
    {
        m_opt_sock.emplace(m_io);
        m_acceptor.async_accept(*m_opt_sock,
            [this](sys::error_code ec)
            {
                if (ec)
                {
                    std::println(
                        "Connection acception error: {}\nStop accepting.",
                        ec.what());
                    return;
                }

                std::println(
                    "New connection from {}", m_opt_sock->remote_endpoint());

                boost::intrusive_ptr session { new Session {
                    m_io, std::move(*m_opt_sock) } };

                asio::co_spawn(m_io, statecoroutines::handshake(session),
                    [session](std::exception_ptr exc_ptr)
                    { session->handle_coroutine_finished(exc_ptr); });

                start();
            });
    }

private:
    asio::io_context &m_io;
    tcp::acceptor m_acceptor;
    std::optional<tcp::socket> m_opt_sock { };
};
