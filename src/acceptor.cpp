#include "acceptor.hpp"
#include "session.hpp"
#include <boost/intrusive_ptr.hpp>
#include <iostream>
#include <print>

Acceptor::Acceptor(boost::asio::io_context &io, uint16_t port)
    : m_io(io)
    , m_acceptor(
          io, boost::asio::ip::tcp::endpoint(boost::asio::ip::tcp::v4(), port))
{
}

void Acceptor::accept()
{
    m_opt_sock.emplace(m_io);
    m_acceptor.async_accept(*m_opt_sock,
        [this](boost::system::error_code ec)
        {
            if (ec)
            {
                std::println("Connection acception error: {}\nStop accepting.",
                    ec.what());
                return;
            }

            std::cout << "New connection from " << m_opt_sock->remote_endpoint()
                      << std::endl;

            boost::intrusive_ptr session { new Session {
                m_io, std::move(*m_opt_sock) } };
            session->begin();
            accept();
        });
}
