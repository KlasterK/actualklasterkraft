module;
#include <boost/asio.hpp>
#include <boost/system.hpp>
#include <memory>
export module actualklasterkraft.packetops;

import actualklasterkraft.errc;

namespace asio = boost::asio;
namespace sys = boost::system;
using asio::ip::tcp;

export namespace packetops
{
    template <typename... Args>
    asio::awaitable<sys::error_code> await_for_packet(
        tcp::socket &socket, Args... args)
    {
        char byte { };
        int32_t packet_size { };
        unsigned position { };
        sys::error_code ec { };

        for (;;)
        {
            co_await asio::async_read(socket, asio::buffer(&byte, 1),
                asio::transfer_exactly(1), asio::redirect_error(ec));
            if (ec)
                co_return ec;

            packet_size |= uint32_t(byte & 0b01111111) << position;
            if ((byte & 0b10000000) == 0)
                break;

            position += 7;
            if (position > 32)
                co_return MCProtocolError::VarIntTooBig;
        }

        if (packet_size > (1 << 23) || packet_size < 1)
            co_return MCProtocolError::MalformedPacketHeader;

        size_t nread
            = co_await asio::async_read(socket, std::make_tuple(args...),
                asio::transfer_exactly(packet_size), asio::redirect_error(ec));
        if (ec)
            co_return ec;

        if (nread < size_t(packet_size))
        {
            auto drop_buf = std::make_unique_for_overwrite<uint8_t[]>(
                packet_size - nread);

            co_await asio::async_read(socket,
                asio::buffer(drop_buf.get(), packet_size - nread),
                asio::transfer_all(), asio::redirect_error(ec));
            if (ec)
                co_return ec;

            co_return MCProtocolError::NotEnoughBuffersToFitPacket;
        }

        co_return { };
    }

    asio::awaitable<sys::error_code> flush_packet(Session &session,
        std::optional<asio::const_buffer> override_buf = std::nullopt)
    {
        sys::error_code ec { };
        std::array<uint8_t, 5> size_buf;
        uint8_t *size_end = size_buf.begin();

        for (uint32_t value = override_buf ? override_buf->size()
                                           : session.get_streambuf().size();
            ;)
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
