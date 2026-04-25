#include "packetops.hpp"
#include "errc.hpp"
#include "session.hpp"
#include <boost/asio.hpp>

namespace asio = boost::asio;
namespace sys = boost::system;

boost::asio::awaitable<std::expected<size_t, boost::system::error_code>>
packetops::await_for_packet(Session &session)
{
    char byte { };
    int32_t packet_size { };
    unsigned position { };
    sys::error_code ec { };

    for (;;)
    {
        co_await asio::async_read(session.get_socket(), asio::buffer(&byte, 1),
            asio::transfer_exactly(1), asio::redirect_error(ec));
        if (ec)
            co_return std::unexpected(ec);

        packet_size |= (byte & 0b01111111) << position;
        if ((byte & 0b10000000) == 0)
            break;

        position += 7;
        if (position > 32)
            co_return std::unexpected(MCProtocolError::VarIntTooBig);
    }

    co_await asio::async_read(session.get_socket(), session.get_streambuf(),
        asio::transfer_exactly(packet_size), asio::redirect_error(ec));

    if (ec)
        co_return std::unexpected(ec);

    co_return packet_size;
}

boost::asio::awaitable<boost::system::error_code> packetops::flush_packet(
    Session &session)
{
    std::array<uint8_t, 5> size_buf;
    auto size_end = size_buf.begin();

    for (uint32_t value = session.get_streambuf().size();;)
    {
        if ((value & ~0x7F) == 0)
        {
            *size_end++ = value & 0xFF;
            break;
        }
        *size_end++ = (value & 0x7F) | 0x80;
        value >>= 7u;
    }

    auto bufs = std::to_array<asio::const_buffer>(
        { { size_buf.begin(),
              static_cast<size_t>(size_end - size_buf.begin()) },
            session.get_streambuf().data() });

    auto [ec, _] = co_await asio::async_write(
        session.get_socket(), bufs, asio::as_tuple);
    session.get_streambuf().consume(session.get_streambuf().size());

    co_return ec;
}
