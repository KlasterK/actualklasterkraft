module;
#include <array>
#include <boost/asio.hpp>
#include <boost/container/small_vector.hpp>
#include <boost/system.hpp>
#include <cstddef>
#include <cstdint>
#include <tuple>
#include <utility>
export module actualklasterkraft.packetops;

import actualklasterkraft.errc;
import actualklasterkraft.protocolprimitives;
import actualklasterkraft.templates;
import actualklasterkraft.transport;

namespace asio = boost::asio;
namespace sys = boost::system;
using namespace protocolprimitives;

constexpr int MaxPacketSizeVarIntByteLength = 3;

export namespace packetops
{
    asio::awaitable<std::tuple<sys::error_code, size_t>> receive_packet_size(
        Transport &transport)
    {
        uint8_t byte { };
        uint32_t result { };
        unsigned position { };

        for (;;)
        {
            auto [ec, _] = co_await asio::async_read(transport.socket,
                asio::buffer(&byte, 1), asio::transfer_all(), asio::as_tuple);
            if (ec)
                co_return std::tuple { ec, 0 };

            result |= uint32_t(byte & 0b01111111) << position;
            if ((byte & 0b10000000) == 0)
                co_return std::tuple { sys::error_code { }, result };

            position += 7;
            if (position == 7 * MaxPacketSizeVarIntByteLength)
                co_return std::tuple { MCProtocolError::VarIntTooBig, 0 };
        }
    }

    template <typename T>
    asio::awaitable<sys::error_code> receive_raw_data(
        Transport &transport, size_t size_limit, T &&buf_or_seq)
    {
        co_return std::get<sys::error_code>(co_await asio::async_read(
            transport.socket, std::forward<T>(buf_or_seq),
            asio::transfer_exactly(size_limit), asio::as_tuple));
    }

    asio::awaitable<sys::error_code> get(
        Transport &transport, asio::streambuf &sb)
    {
        auto [ec, size] = co_await receive_packet_size(transport);
        if (ec)
            co_return ec;

        co_return co_await receive_raw_data(transport, size, sb);
    }

    asio::awaitable<std::tuple<sys::error_code, size_t>> get(
        Transport &transport, asio::mutable_buffer buf)
    {
        auto [ec, size] = co_await receive_packet_size(transport);
        if (ec)
            co_return std::tuple { ec, size };
        if (size > buf.size())
            co_return std::tuple { MCProtocolError::BufferTooSmallForPacket,
                size };

        ec = co_await receive_raw_data(transport, size, buf);
        if (ec)
            co_return std::tuple { ec, size };

        co_return std::tuple { sys::error_code { }, size };
    }

    template <typename T>
    asio::awaitable<sys::error_code> send_raw_data(
        Transport &transport, T &&buf_or_seq)
    {
        co_return InlineTie(TieReturn, std::ignore)
            = co_await asio::async_write(
                transport.socket, std::forward<T>(buf_or_seq), asio::as_tuple);
    }

    asio::awaitable<sys::error_code> put(
        Transport &transport, asio::const_buffer buf)
    {
        std::array<uint8_t, 5> size_buf;
        auto size_end = write_var<uint32_t>(size_buf.begin(), buf.size());
        size_t size_len = size_end - size_buf.begin();
        if (size_len > MaxPacketSizeVarIntByteLength)
            co_return MCProtocolError::VarIntTooBig;

        std::array send_bufs { asio::const_buffer { size_buf.data(), size_len },
            buf };
        co_return co_await send_raw_data(transport, send_bufs);
    }

    asio::awaitable<sys::error_code> put(
        Transport &transport, asio::streambuf &sb)
    {
        std::array<uint8_t, 5> size_buf;
        auto size_end = write_var<uint32_t>(size_buf.begin(), sb.size());
        size_t size_len = size_end - size_buf.begin();
        if (size_len > MaxPacketSizeVarIntByteLength)
            co_return MCProtocolError::VarIntTooBig;

        boost::container::small_vector<asio::const_buffer, 2> send_bufs;
        send_bufs.emplace_back(size_buf.data(), size_len);

        auto sb_bufs = sb.data();
        std::copy(asio::buffer_sequence_begin(sb_bufs),
            asio::buffer_sequence_end(sb_bufs), std::back_inserter(send_bufs));

        auto ec = co_await send_raw_data(transport, send_bufs);
        sb.consume(sb.size());
        co_return ec;
    }

    template <size_t N>
    asio::awaitable<sys::error_code> put(
        Transport &transport, std::span<asio::const_buffer, N> bufs)
    {
        std::array<uint8_t, 5> size_buf;
        auto size_end = write_var<uint32_t>(size_buf.begin(), bufs.size());
        size_t size_len = size_end - size_buf.begin();
        if (size_len > MaxPacketSizeVarIntByteLength)
            co_return MCProtocolError::VarIntTooBig;

        std::array<asio::const_buffer, N + 1> send_bufs { asio::const_buffer {
            size_buf.data(), size_len } };
        std::move(send_bufs.begin() + 1, send_bufs.end(), bufs.begin());
        co_return co_await send_raw_data(transport, send_bufs);
    }

    template <typename... Args>
    asio::awaitable<sys::error_code> put_va(Transport &transport, Args... args)
    {
        std::array<asio::const_buffer, sizeof...(Args) + 1> send_bufs {
            asio::const_buffer { }, asio::buffer(args)...
        };

        std::array<uint8_t, 5> size_buf;
        auto size_end = write_var<uint32_t>(
            size_buf.begin(), asio::buffer_size(send_bufs));
        size_t size_len = size_end - size_buf.begin();
        if (size_len > MaxPacketSizeVarIntByteLength)
            co_return MCProtocolError::VarIntTooBig;
        send_bufs[0] = { size_buf.data(), size_len };

        co_return co_await send_raw_data(transport, send_bufs);
    }
}
