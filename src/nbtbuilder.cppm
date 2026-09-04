module;
#include <cassert>
#include <concepts>
#include <cstdint>
#include <string_view>
export module actualklasterkraft.nbtbuilder;

import actualklasterkraft.protocolprimitives;

export namespace nbttags
{
    constexpr uint8_t End = 0;
    constexpr uint8_t I8 = 1;
    constexpr uint8_t I16 = 2;
    constexpr uint8_t I32 = 3;
    constexpr uint8_t I64 = 4;
    constexpr uint8_t Float = 5;
    constexpr uint8_t Double = 6;
    constexpr uint8_t I8Array = 7;
    constexpr uint8_t String = 8;
    constexpr uint8_t List = 9;
    constexpr uint8_t Compound = 10;
    constexpr uint8_t I32Array = 11;
    constexpr uint8_t I64Array = 12;

    using ListLen = int32_t;
}

export template <typename OutputIt> struct NBTBuilder
{
    OutputIt it { };

    template <std::integral T> NBTBuilder &operator<<(T i)
    {
        it = protocolprimitives::write_integer(it, i);
        return *this;
    }

    template <std::floating_point T> NBTBuilder &operator<<(T r)
    {
        it = protocolprimitives::write_real(it, r);
        return *this;
    }

    NBTBuilder &operator<<(std::string_view sv)
    {
        assert(sv.size() < 65500);
        it = protocolprimitives::write_integer<uint16_t>(it, sv.size());
        it = std::copy(sv.begin(), sv.end(), it);
        return *this;
    }
};
