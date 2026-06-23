module;
#include <boost/asio.hpp>
#include <boost/intrusive_ptr.hpp>
#include <boost/smart_ptr/intrusive_ref_counter.hpp>
#include <boost/system.hpp>
#include <exception>
#include <print>
export module actualklasterkraft.session;

import actualklasterkraft.streambufops;

namespace asio = boost::asio;
namespace sys = boost::system;
using asio::ip::tcp;

export class Session
    : public boost::intrusive_ref_counter<Session, boost::thread_unsafe_counter>
{
public:
    Session(asio::io_context &io, tcp::socket &&sock);
    void handle_coroutine_finished(
        std::exception_ptr exc_ptr, sys::error_code ec);

    asio::io_context &get_io() { return m_io; }
    const asio::io_context &get_io() const { return m_io; }

    tcp::socket &get_socket() { return m_sock; }
    const tcp::socket &get_socket() const { return m_sock; }

    asio::streambuf &get_streambuf() { return m_streambuf; }
    const asio::streambuf &get_streambuf() const { return m_streambuf; }

    void print_streambuf();

private:
    asio::io_context &m_io;
    tcp::socket m_sock;
    asio::streambuf m_streambuf;

    std::string m_remote_endpoint_name;
};

// +----------------+
// | IMPLEMENTATION |
// +----------------+

void Session::print_streambuf()
{
    for (std::print("\t");;)
    {
        int byte = m_streambuf.sbumpc();
        if (byte < 0)
            break;

        std::print("{:02x} ", byte);
    }
    m_streambuf.consume(m_streambuf.size());
    std::println();
}

Session::Session(asio::io_context &io, tcp::socket &&sock)
    : m_io(io)
    , m_sock(std::move(sock))
{
    std::ostringstream oss;
    oss << m_sock.remote_endpoint();
    m_remote_endpoint_name = oss.str();
}

void Session::handle_coroutine_finished(
    std::exception_ptr exc_ptr, sys::error_code ec)
{
    if (exc_ptr)
        std::rethrow_exception(exc_ptr);

    if (ec)
    {
        std::println("Connection from {} caused an error: {}",
            m_remote_endpoint_name, ec.what());
        m_sock.close();
        return;
    }

    if (!m_sock.is_open())
    {
        std::println(
            "Connection {} was successfully closed", m_remote_endpoint_name);
        return;
    }
}
