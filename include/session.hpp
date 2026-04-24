#ifndef ACTKK_SESSION_HPP
#define ACTKK_SESSION_HPP

#include "statecoroutines.hpp"
#include <boost/asio.hpp>
#include <boost/smart_ptr/intrusive_ref_counter.hpp>
#include <boost/system.hpp>
#include <exception>

class Session
    : public boost::intrusive_ref_counter<Session, boost::thread_unsafe_counter>
{
public:
    Session(boost::asio::io_context &io, boost::asio::ip::tcp::socket &&sock);
    void begin();
    void handle_coroutine_finished(
        std::exception_ptr exc_ptr, boost::system::error_code ec);

    boost::asio::io_context &get_io() { return m_io; }
    const boost::asio::io_context &get_io() const { return m_io; }

    boost::asio::ip::tcp::socket &get_socket() { return m_sock; }
    const boost::asio::ip::tcp::socket &get_socket() const { return m_sock; }

    boost::asio::streambuf &get_streambuf() { return m_streambuf; }
    const boost::asio::streambuf &get_streambuf() const { return m_streambuf; }

private:
    void print_streambuf();

    boost::asio::io_context &m_io;
    boost::asio::ip::tcp::socket m_sock;
    boost::asio::streambuf m_streambuf;

    std::string m_remote_endpoint_name;
};

#endif // ACTKK_SESSION_HPP
