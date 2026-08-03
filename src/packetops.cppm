module;
#include <boost/asio.hpp>
#include <boost/system.hpp>
export module actualklasterkraft.packetops;

import actualklasterkraft.errc;
import actualklasterkraft.session;

namespace asio = boost::asio;
namespace sys = boost::system;

export namespace packetops
{
    asio::awaitable<sys::error_code> await_for_packet(Session &session)
    {
        char byte { };
        int32_t packet_size { };
        unsigned position { };
        sys::error_code ec { };

        for (;;)
        {
            co_await asio::async_read(session.get_socket(),
                asio::buffer(&byte, 1), asio::transfer_exactly(1),
                asio::redirect_error(ec));
            if (ec)
                co_return ec;

            packet_size |= uint32_t(byte & 0b01111111) << position;
            if ((byte & 0b10000000) == 0)
                break;

            position += 7;
            if (position > 32)
                co_return MCProtocolError::VarIntTooBig;
        }

        co_await asio::async_read(session.get_socket(), session.get_streambuf(),
            asio::transfer_exactly(packet_size), asio::redirect_error(ec));

        co_return ec;
    }

    asio::awaitable<sys::error_code> flush_packet(Session &session,
        std::optional<asio::const_buffer> override_buf = std::nullopt)
    {
        sys::error_code ec { };
        std::array<uint8_t, 5> size_buf;
        uint8_t *size_end = size_buf.begin();

        for (uint32_t value = override_buf ? override_buf->size() : session.get_streambuf().size();;)
        {
            if ((value & ~0x7F) == 0)
            {
                *size_end++ = value & 0xFF;
                break;
            }
            *size_end++ = (value & 0x7F) | 0x80;
            value >>= 7u;
        }

        std::array<asio::const_buffer, 2> bufs {
            asio::buffer(size_buf.begin(), size_end - size_buf.begin()),
            override_buf ? *override_buf : session.get_streambuf().data()
        };

        co_await asio::async_write(
            session.get_socket(), bufs, asio::redirect_error(ec));

        if (!override_buf.has_value())
            session.get_streambuf().consume(session.get_streambuf().size());

        co_return ec;
    }
}
