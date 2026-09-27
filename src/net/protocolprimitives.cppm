module;
#include <bit>
#include <boost/endian.hpp>
#include <boost/system.hpp>
#include <concepts>
#include <cstdint>
#include <iterator>
#include <limits>
#include <string_view>
#include <tuple>
#include <numbers>
#include <algorithm>
export module actualklasterkraft.protocolprimitives;

import actualklasterkraft.errc;
import actualklasterkraft.world.math;

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

template <typename T>
concept Number = std::integral<T> || std::floating_point<T>;

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
    constexpr It write_var(It it, std::type_identity_t<T> value)
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

    template <Number T, InputIt8 It, std::sentinel_for<It> End>
    [[nodiscard]] constexpr std::tuple<T, It, sys::error_code> read_number(
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

    template <Number T, OutputIt8 It> constexpr It write_number(It it, T value)
    {
        std::array<uint8_t, sizeof(T)> bytes;
        en::endian_store<T, sizeof(T), en::order::big>(bytes.data(), value);
        return std::copy(bytes.begin(), bytes.end(), it);
    }

    template <OutputIt8 It>
    constexpr It write_string(It it, std::string_view string)
    {
        it = write_var<uint32_t>(it, string.size());
        return std::transform(string.begin(), string.end(), it,
            [](auto c) { return uint8_t(c); });
    }

    template <std::floating_point T, OutputIt8 It>
    constexpr It write_xyz(It it, Vec3<T> value)
    {
        it = write_number(it, value.x);
        it = write_number(it, value.y);
        it = write_number(it, value.z);
        return it;
    }

    template <std::floating_point T, OutputIt8 It>
    constexpr It write_xz(It it, Vec2<T> value)
    {
        it = write_number(it, value.x);
        it = write_number(it, value.z);
        return it;
    }

    template <OutputIt8 It> constexpr It write_angle256(It it, Angle value)
    {
        constexpr float tau = std::numbers::pi_v<float> * 2;
        *it++ = static_cast<uint8_t>(
            value.wrap_unsigned().as_radians() / tau * 256);
        return it;
    }
}
