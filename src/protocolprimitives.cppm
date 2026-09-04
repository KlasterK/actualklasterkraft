module;
#include <bit>
#include <boost/endian.hpp>
#include <boost/system.hpp>
#include <concepts>
#include <cstdint>
#include <iterator>
#include <string_view>
#include <tuple>
export module actualklasterkraft.protocolprimitives;

import actualklasterkraft.errc;

namespace sys = boost::system;
namespace en = boost::endian;

template <typename T>
concept InputIt8 = std::input_iterator<T>
    && (std::same_as<uint8_t, typename std::iterator_traits<T>::value_type>
        || std::same_as<char, typename std::iterator_traits<T>::value_type>);

template <typename T>
concept OutputIt8
    = std::output_iterator<T, uint8_t> || std::output_iterator<T, char>;

template <typename T>
concept Integral3264 = std::integral<T> && (sizeof(T) == 4 || sizeof(T) == 8);

export namespace protocolprimitives
{
    template <Integral3264 T, InputIt8 It, std::sentinel_for<It> End>
    [[nodiscard]] constexpr std::tuple<T, It, sys::error_code> read_var(
        It it, End end)
    {
        uint8_t byte { };
        std::make_unsigned_t<T> value { };
        unsigned position { };

        for (;;)
        {
            if (it == end)
                return { 0, it, MCProtocolError::UnsufficientPacketData };

            byte = uint8_t(*it);
            ++it;

            value |= std::make_unsigned_t<T>(byte & 0x7F) << position;
            if ((byte & 0x80) == 0)
                return { T(value), it, { } };

            position += 7;
            if (position == 7 + 7 * sizeof(T))
                return { 0, it, MCProtocolError::VarIntTooBig };
        }
    }

    template <Integral3264 T, OutputIt8 It>
    constexpr It write_var(It it, T value)
    {
        for (;;)
        {
            if ((value & ~0x7F) == 0)
            {
                *it++ = uint8_t(value & 0xFF);
                return it;
            }

            *it++ = uint8_t((value & 0x7F) | 0x80);
            value = T(std::make_unsigned_t<T>(value) >> 7u);
        }
    }

    template <std::integral T, InputIt8 It, std::sentinel_for<It> End>
    [[nodiscard]] constexpr std::tuple<T, It, sys::error_code> read_integer(
        It it, End end)
    {
        std::array<uint8_t, sizeof(T)> bytes;
        for (uint8_t &byte : bytes)
        {
            if (it == end)
                return { 0, it, MCProtocolError::UnsufficientPacketData };

            byte = uint8_t(*it);
            ++it;
        }
        T value = en::endian_load<T, sizeof(T), en::order::big>(bytes.data());
        return { value, it, { } };
    }

    template <std::integral T, OutputIt8 It>
    constexpr It write_integer(It it, T value)
    {
        std::array<uint8_t, sizeof(T)> bytes;
        en::endian_store<T, sizeof(T), en::order::big>(bytes.data(), value);
        return std::copy(bytes.begin(), bytes.end(), it);
    }

    template <std::floating_point T, InputIt8 It, std::sentinel_for<It> End>
    [[nodiscard]] constexpr std::tuple<T, It, sys::error_code> read_real(
        It it, End end)
    {
        std::array<uint8_t, sizeof(T)> bytes;
        for (uint8_t &byte : bytes)
        {
            if (it == end)
                return { T { }, it, MCProtocolError::UnsufficientPacketData };

            byte = uint8_t(*it);
            ++it;
        }
        return { std::bit_cast<T>(bytes), it, { } };
    }

    template <std::floating_point T, OutputIt8 It>
    constexpr It write_real(It it, T value)
    {
        auto bytes = std::bit_cast<std::array<uint8_t, sizeof(T)>>(value);
        return std::copy(bytes.begin(), bytes.end(), it);
    }

    template <OutputIt8 It>
    constexpr It write_string(It it, std::string_view string)
    {
        it = write_var<uint32_t>(it, string.size());
        return std::transform(string.begin(), string.end(), it,
            [](auto c) { return uint8_t(c); });
    }
}
