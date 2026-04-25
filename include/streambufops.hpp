#ifndef ACTKK_STREAMBUFOPS_HPP
#define ACTKK_STREAMBUFOPS_HPP

#include "errc.hpp"
#include <boost/asio.hpp>
#include <boost/endian.hpp>
#include <boost/endian/conversion.hpp>
#include <cstdint>
#include <expected>
#include <optional>

class Session;

namespace streambufops
{

    inline std::expected<int32_t, boost::system::error_code> read_vari32(
        boost::asio::streambuf &streambuf)
    {
        int byte { };
        int32_t value { };
        unsigned position { };

        for (;;)
        {
            byte = streambuf.sbumpc();
            if (byte < 0)
                return std::unexpected(boost::asio::error::eof);

            value |= (byte & 0x7F);
            if ((byte & 0x80) == 0)
                break;

            position += 7;
            if (position > 32)
                return std::unexpected(MCProtocolError::VarIntTooBig);
        }
        return value;
    }

    inline void write_vari32(boost::asio::streambuf &streambuf, int32_t value)
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
    inline std::optional<T> read_integer(boost::asio::streambuf &streambuf)
    {
        T value { };
        if (streambuf.sgetn(reinterpret_cast<char *>(&value), sizeof(T))
            < std::streamsize(sizeof(T)))
            return std::nullopt;

        return boost::endian::big_to_native(value);
    }

    template <std::integral T>
    inline void write_integer(boost::asio::streambuf &streambuf, T value)
    {
        value = boost::endian::native_to_big(value);
        streambuf.sputn(reinterpret_cast<char *>(&value), sizeof(T));
    }
}

#endif // ACTKK_STREAMBUFOPS_HPP
