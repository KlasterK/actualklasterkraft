module;
#include <boost/asio.hpp>
#include <boost/system.hpp>
#include <exception>
#include <print>
export module actualklasterkraft.net.base.acceptor;

import actualklasterkraft.generic.completiontokens;
import actualklasterkraft.generic.formatters;
import actualklasterkraft.net.base.transport;
import actualklasterkraft.net.base.statecoroutines;

namespace asio = boost::asio;
namespace sys = boost::system;
using asio::ip::tcp;

export class Acceptor
{
public:
    Acceptor(asio::any_io_executor io, uint16_t port)
        : m_acceptor(io, tcp::endpoint(tcp::v4(), port))
        , m_socket(io)
    {
    }

    void start()
    {
        m_acceptor.async_accept(m_socket,
            [this](sys::error_code ec)
            {
                if (ec)
                {
                    std::println(
                        "Connection acception error: {}\nStop accepting.", ec);
                    return;
                }

                Transport transport { std::move(m_socket), ec };
                if (ec)
                {
                    std::println(
                        "Transport initialisation error: {}\nStop accepting.",
                        ec);
                    return;
                }

                asio::co_spawn(m_acceptor.get_executor(),
                    statecoroutines::handshake(std::move(transport)),
                    detached_log_exceptions_token);

                start();
            });
    }

private:
    tcp::acceptor m_acceptor;
    tcp::socket m_socket;
};
