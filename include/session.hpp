#ifndef ACTKK_SESSION_HPP
#define ACTKK_SESSION_HPP

#include <boost/asio.hpp>
#include <boost/smart_ptr/intrusive_ref_counter.hpp>
#include <expected>

class Session
    : public boost::intrusive_ref_counter<Session, boost::thread_unsafe_counter>
{
public:
    Session(boost::asio::io_context &io, boost::asio::ip::tcp::socket &&sock);
    void next_packet();

private:
    void print_streambuf();

    /// Awaits for a packet and reads it into the streambuf
    /// (without Packet Length).
    ///
    /// @returns Packet size or error code.
    boost::asio::awaitable<std::expected<size_t, boost::system::error_code>>
    await_for_packet();

    boost::asio::awaitable<boost::system::error_code> flush_packet();

    boost::asio::awaitable<boost::system::error_code> state_handshake();

    boost::asio::awaitable<boost::system::error_code> state_status();

private:
    boost::asio::io_context &m_io;
    boost::asio::ip::tcp::socket m_sock;
    boost::asio::streambuf m_streambuf;

    std::string m_remote_endpoint_name;
    boost::asio::awaitable<boost::system::error_code> (Session::*m_state_cb)()
        = &Session::state_handshake;
};

#endif // ACTKK_SESSION_HPP
