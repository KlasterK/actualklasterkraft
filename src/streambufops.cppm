module;
#include <boost/asio.hpp>
#include <boost/endian.hpp>
#include <cstdint>
#include <expected>
#include <optional>
export module actualklasterkraft.streambufops;

import actualklasterkraft.errc;

namespace asio = boost::asio;
namespace sys = boost::system;

export namespace streambufops
{
    std::expected<int32_t, sys::error_code> read_vari32(
        asio::streambuf &streambuf)
    {
        int byte { };
        int32_t value { };
        unsigned position { };

        for (;;)
        {
            byte = streambuf.sbumpc();
            if (byte < 0)
                return std::unexpected(asio::error::eof);

            value |= (byte & 0x7F);
            if ((byte & 0x80) == 0)
                break;

            position += 7;
            if (position > 32)
                return std::unexpected(MCProtocolError::VarIntTooBig);
        }
        return value;
    }

    void write_vari32(asio::streambuf &streambuf, int32_t value)
    {
        for (;;)
        {
            if ((value & ~0x7F) == 0)
            {
                streambuf.sputc(value & 0xFF);
                return;
            }

            streambuf.sputc((value & 0x7F) | 0x80);
            value = int32_t(uint32_t(value) >> 7u);
        }
    }

    template <std::integral T>
    std::optional<T> read_integer(asio::streambuf &streambuf)
    {
        T value { };
        if (streambuf.sgetn(reinterpret_cast<char *>(&value), sizeof(T))
            < static_cast<std::streamsize>(sizeof(T)))
            return std::nullopt;

        return boost::endian::big_to_native(value);
    }

    template <std::integral T>
    void write_integer(boost::asio::streambuf &streambuf, T value)
    {
        value = boost::endian::native_to_big(value);
        streambuf.sputn(reinterpret_cast<char *>(&value), sizeof(T));
    }
}
