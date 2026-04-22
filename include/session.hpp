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

    std::expected<int32_t, boost::system::error_code> read_vari32();
    template <std::integral T> std::optional<T> read_integer();

    boost::asio::awaitable<boost::system::error_code> flush_packet();

    void write_vari32(int32_t value);
    template <std::integral T> void write_integer(T value);

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

template <std::integral T> inline std::optional<T> Session::read_integer()
{
    T value { };
    if (m_streambuf.sgetn(reinterpret_cast<char *>(&value), sizeof(T))
        < sizeof(T))
        return std::nullopt;

    return value;
}

template <std::integral T> inline void Session::write_integer(T value)
{
    m_streambuf.sputn(reinterpret_cast<char *>(&value), sizeof(T));
}

#endif // ACTKK_SESSION_HPP
