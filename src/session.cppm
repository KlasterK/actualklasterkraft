module;
#include <boost/asio.hpp>
#include <boost/intrusive_ptr.hpp>
#include <boost/smart_ptr/intrusive_ref_counter.hpp>
#include <boost/system.hpp>
#include <exception>
#include <format>
#include <print>
export module actualklasterkraft.session;

import actualklasterkraft.formatters;

namespace asio = boost::asio;
namespace sys = boost::system;
using asio::ip::tcp;

export class Session
    : public boost::intrusive_ref_counter<Session, boost::thread_unsafe_counter>
{
public:
    Session(asio::io_context &io, tcp::socket &&sock)
        : m_io(io)
        , m_sock(std::move(sock))
        , m_remote_endpoint_name(std::format("{}", m_sock.remote_endpoint()))
    {
    }

    void handle_coroutine_finished(std::exception_ptr exc_ptr)
    {
        if (exc_ptr)
            std::rethrow_exception(exc_ptr);

        if (!m_sock.is_open())
        {
            std::println("Connection {} was closed", m_remote_endpoint_name);
            return;
        }
    }

    asio::io_context &get_io() { return m_io; }
    const asio::io_context &get_io() const { return m_io; }

    tcp::socket &get_socket() { return m_sock; }
    const tcp::socket &get_socket() const { return m_sock; }

    asio::streambuf &get_streambuf() { return m_streambuf; }
    const asio::streambuf &get_streambuf() const { return m_streambuf; }

    void print_streambuf()
    {
        for (std::print("\t");;)
        {
            int byte = m_streambuf.sbumpc();
            if (byte < 0)
                break;

            std::print("{:02x} ", byte);
        }
        std::println();
    }

private:
    asio::io_context &m_io;
    tcp::socket m_sock;
    asio::streambuf m_streambuf { };
    std::string m_remote_endpoint_name { };
};
