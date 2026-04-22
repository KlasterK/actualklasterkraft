#ifndef ACTKK_ACCEPTOR_HPP
#define ACTKK_ACCEPTOR_HPP

#include <boost/asio.hpp>

class Acceptor
{
public:
    Acceptor(boost::asio::io_context &io, uint16_t port);
    void accept();

private:
    boost::asio::io_context &m_io;
    boost::asio::ip::tcp::acceptor m_acceptor;
    std::optional<boost::asio::ip::tcp::socket> m_opt_sock { };
};

#endif // ACTKK_ACCEPTOR_HPP
