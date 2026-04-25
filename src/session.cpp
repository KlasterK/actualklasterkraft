#include "session.hpp"
#include "statecoroutines.hpp"
#include "streambufops.hpp"
#include <boost/intrusive_ptr.hpp>
#include <print>

using boost::asio::ip::tcp;

auto getlmb() { }

void Session::print_streambuf()
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

Session::Session(
    boost::asio::io_context &io, boost::asio::ip::tcp::socket &&sock)
    : m_io(io)
    , m_sock(std::move(sock))
{
    std::ostringstream oss;
    oss << m_sock.remote_endpoint();
    m_remote_endpoint_name = oss.str();
}

void Session::begin()
{
    boost::asio::co_spawn(m_io,
        statecoroutines::handshake(boost::intrusive_ptr(this)),
        [self = boost::intrusive_ptr(this)](
            std::exception_ptr exc_ptr, boost::system::error_code ec)
        { self->handle_coroutine_finished(exc_ptr, ec); });
}

void Session::handle_coroutine_finished(
    std::exception_ptr exc_ptr, boost::system::error_code ec)
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
