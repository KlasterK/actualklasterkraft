module;
#include <boost/asio.hpp>
#include <boost/endian.hpp>
#include <boost/system.hpp>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <iterator>
#include <optional>
#include <stdexcept>
#include <string_view>
#include <tuple>
export module actualklasterkraft.streambufops;

import actualklasterkraft.errc;

namespace asio = boost::asio;
namespace sys = boost::system;

template <typename T>
concept InputIt8 = std::input_iterator<T>
    && (std::same_as<uint8_t, typename std::iterator_traits<T>::value_type>
        || std::same_as<char, typename std::iterator_traits<T>::value_type>);

template <typename T>
concept OutputIt8
    = std::output_iterator<T, uint8_t> || std::output_iterator<T, char>;

export namespace protocoltypes
{
    template <InputIt8 It>
    [[nodiscard]] constexpr std::tuple<int32_t, It, sys::error_code> read_v32(
        It it, It end)
    {
        uint8_t byte { };
        int32_t value { };
        unsigned position { };

        for (;;)
        {
            if (it == end)
                return { 0, it, MCProtocolError::UnsufficientPacketData };

            byte = uint8_t(*it++);

            value |= uint32_t(byte & 0x7F) << position;
            if ((byte & 0x80) == 0)
                break;

            position += 7;
            if (position > 31)
                return { 0, it, MCProtocolError::VarIntTooBig };
        }

        return { value, it, { } };
    }

    template <OutputIt8 It> constexpr It write_v32(It it, int32_t value)
    {
        for (;;)
        {
            if ((value & ~0x7F) == 0)
            {
                *it++ = uint8_t(value & 0xFF);
                return it;
            }

            *it++ = uint8_t((value & 0x7F) | 0x80);
            value = int32_t(uint32_t(value) >> 7u);
        }
        return it;
    }

    template <std::integral T, InputIt8 It>
    [[nodiscard]] std::tuple<T, It, sys::error_code> read_integer(It it, It end)
    {
        T value { };
        std::span<uint8_t> span { reinterpret_cast<uint8_t *>(&value),
            sizeof(T) };

        for (auto span_it = span.begin(); span_it != span.end();)
        {
            if (it == end)
                return { 0, it, MCProtocolError::UnsufficientPacketData };
            *span_it++ = uint8_t(*it++);
        }

        return { boost::endian::big_to_native(value), it, { } };
    }

    template <std::integral T, OutputIt8 It> It write_integer(It it, T value)
    {
        value = boost::endian::native_to_big(value);
        return std::copy_n(
            reinterpret_cast<const uint8_t *>(&value), sizeof(T), it);
    }

    template <OutputIt8 It>
    constexpr It write_string(It it, std::string_view string)
    {
        it = write_v32(it, string.size());
        if consteval
        {
            return std::transform(string.begin(), string.end(), it,
                [](auto c) { return uint8_t(c); });
        }
        return std::copy_n(reinterpret_cast<const uint8_t *>(string.data()),
            string.size(), it);
    }

    template <std::floating_point T, OutputIt8 It> It write_real(It it, T value)
    {
        return std::copy_n(
            reinterpret_cast<const uint8_t *>(&value), sizeof(T), it);
    }
}

using ISI = std::istreambuf_iterator<char>;
using OSI = std::ostreambuf_iterator<char>;

export namespace streambufops
{
    [[nodiscard]] int32_t read_v32(asio::streambuf &sb, sys::error_code &out_ec)
    {
        int32_t value { };
        std::tie(value, std::ignore, out_ec)
            = protocoltypes::read_v32(ISI(&sb), ISI());
        return value;
    }

    void write_v32(asio::streambuf &sb, int32_t value)
    {
        protocoltypes::write_v32(OSI(&sb), value);
    }

    template <std::integral T>
    [[nodiscard]] T read_integer(asio::streambuf &sb, sys::error_code &out_ec)
    {
        T value { };
        std::tie(value, std::ignore, out_ec)
            = protocoltypes::read_integer<T>(ISI(&sb), ISI());
        return value;
    }

    template <std::integral T> void write_integer(asio::streambuf &sb, T value)
    {
        protocoltypes::write_integer<T>(OSI(&sb), value);
    }

    void write_string(asio::streambuf &sb, std::string_view string)
    {
        protocoltypes::write_string(OSI(&sb), string);
    }

    template <std::floating_point T>
    void write_real(asio::streambuf &sb, T value)
    {
        protocoltypes::write_real<T>(OSI(&sb), value);
    }
}
