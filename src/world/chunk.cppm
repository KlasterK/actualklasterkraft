module;
#include <algorithm>
#include <array>
#include <cassert>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <ranges>
#include <span>
export module actualklasterkraft.world.chunk;

import actualklasterkraft.basepool;
import actualklasterkraft.protocolprimitives;
import actualklasterkraft.world.math;

using namespace protocolprimitives;

template <typename T, typename... Args>
    requires(std::convertible_to<Args, T> && ...)
consteval auto guarantee_sorted_array(Args... args)
{
    std::array<T, sizeof...(Args)> array { static_cast<T>(args)... };
    if (!std::ranges::is_sorted(array))
        throw;
    return array;
}

/******************************************************************************/

export namespace palette
{
    struct DirectFormat
    {
    };

    struct SingleValueFormat
    {
        uint32_t value { };
    };

    struct LocalPaletteFormat
    {
        std::span<const uint32_t> palette;
    };

    using FormatVariant = std::variant<palette::DirectFormat,
        palette::SingleValueFormat, palette::LocalPaletteFormat>;

    constexpr auto AnyAirPalette
        = guarantee_sorted_array<uint32_t>(0, 15292, 15293);

    constexpr std::array<uint32_t, 0> AnyFluidPalette; // FIXME: fill

    bool is_air(uint32_t v)
    {
        return std::ranges::binary_search(palette::AnyAirPalette, v);
    }

    bool is_fluid(uint32_t v)
    {
        return std::ranges::binary_search(palette::AnyFluidPalette, v);
    }
}

/******************************************************************************/

export namespace palette
{
    template <size_t Extent, typename EntryInt>
    uint32_t get(const palette::FormatVariant &format, const EntryInt *data,
        Vec3<int> pos)
    {
        assert(pos.x >= 0 && pos.x < Extent && pos.y >= 0 && pos.y < Extent
            && pos.z >= 0 && pos.z < Extent);

        if (auto *fmt = std::get_if<palette::SingleValueFormat>(&format))
            return fmt->value;
        if (auto *fmt = std::get_if<palette::LocalPaletteFormat>(&format))
            return fmt->palette[data[pos.x + pos.z * Extent
                + pos.y * Extent * Extent]];
        return data[pos.x + pos.z * Extent + pos.y * Extent * Extent];
    }

    template <size_t Extent, typename EntryInt>
    void convert_to_direct(palette::FormatVariant &format, EntryInt *data)
    {
        if (auto *fmt = std::get_if<palette::SingleValueFormat>(&format))
        {
            for (size_t i { }; i < Extent * Extent * Extent; ++i)
                data[i] = fmt->value;
        }
        else if (auto *fmt = std::get_if<palette::LocalPaletteFormat>(&format))
        {
            for (size_t i { }; i < Extent * Extent * Extent; ++i)
                data[i] = fmt->palette[data[i]];
        }
        format = palette::DirectFormat { };
    }

    template <size_t Extent, typename EntryInt>
    void set_direct(palette::FormatVariant &format, EntryInt *data,
        Vec3<int> pos, EntryInt value)
    {
        assert(pos.x >= 0 && pos.x < Extent && pos.y >= 0 && pos.y < Extent
            && pos.z >= 0 && pos.z < Extent);

        convert_to_direct(format, data);
        m_states[pos.x + pos.z * Extent + pos.y * Extent * Extent] = value;
    }

    template <size_t Extent, typename EntryInt>
    void set_local_paletted_fallback_to_direct(Vec3<int> pos,
        palette::FormatVariant &format, EntryInt *data,
        std::span<const uint32_t> palette, size_t index)
    {
        assert(pos.x >= 0 && pos.x < Extent && pos.y >= 0 && pos.y < Extent
            && pos.z >= 0 && pos.z < Extent && index < palette.size());

        if (auto *fmt = std::get_if<palette::LocalPaletteFormat>(&format);
            fmt == nullptr || fmt->palette.data() != palette.data()
            || fmt->palette.size() != palette.size())
        {
            set_direct(pos, palette[index]);
            return;
        }

        m_states[pos.x + pos.z * Extent + pos.y * Extent * Extent] = index;
    }

    auto serialize(auto it)
    {
        if (auto *fmt = std::get_if<palette::SingleValueFormat>(&m_format))
        {
            it = write_number(it, uint8_t(0));
            it = write_var<uint32_t>(it, fmt->value);
            return it;
        }

        int bits_per_entry = DirectBitsPerEntry;
        std::span<const uint32_t> palette_opt;

        if (auto *fmt = std::get_if<palette::LocalPaletteFormat>(&m_format))
        {
            bits_per_entry = std::max(MinLocalPaletteBitsPerEntry,
                std::bit_width(fmt->palette.size() - 1));
            palette_opt = fmt->palette;
        }

        it = write_number(it, uint8_t(bits_per_entry));
        if (palette_opt.data())
        {
            it = write_var<uint32_t>(it, palette_opt.size());
            for (uint32_t entry : palette_opt)
                it = write_var<uint32_t>(it, entry);
        }

        int entries_per_u64 = 64 / bits_per_entry;
        int u64s_count
            = (m_states.size() + entries_per_u64 - 1) / entries_per_u64;
        for (int i { }; i < u64s_count; ++i)
        {
            uint64_t storage { };
            for (int j { entries_per_u64 }; j-- > 0;)
            {
                if (i * entries_per_u64 + j >= m_states.size())
                    continue;
                storage <<= bits_per_entry;
                storage |= m_states[i * entries_per_u64 + j];
            }
            it = write_number(it, storage);
        }
        return it;
    }
}

/******************************************************************************/

export class ChunkSection
{
public:
    ChunkSection(palette::FormatVariant block_states_fmt,
        palette::FormatVariant biomes_fmt)
        : m_block_states(block_states_fmt)
        , m_biomes(biomes_fmt)
        , m_block_count(is_air(m_block_states.get({ })) ? 16 * 16 * 16 : 0)
        , m_fluid_count(is_fluid(m_block_states.get({ })) ? 16 * 16 * 16 : 0)
    {
    }

    uint32_t get_block(Vec3<int> pos16) const
    {
        return m_block_states.get(pos16);
    }

    void set_block_single_value(uint32_t value)
    {
        m_block_states.set_single_value(value);
        m_block_count = is_air(value) ? 16 * 16 * 16 : 0;
        m_fluid_count = is_fluid(value) ? 16 * 16 * 16 : 0;
    }

    void set_block_paletted(
        Vec3<int> pos16, std::span<const uint32_t> palette, size_t index)
    {
        inc_dec_counts(m_block_states.get(pos16), palette[index]);
        m_block_states.set_paletted(pos16, palette, index);
    }

    void set_block_direct(Vec3<int> pos16, uint16_t value)
    {
        inc_dec_counts(m_block_states.get(pos16), value);
        m_block_states.set_direct(pos16, value);
    }

    uint32_t get_biome(Vec3<int> pos4) const { return m_biomes.get(pos4); }

    void set_biome_single_value(uint32_t value)
    {
        m_biomes.set_single_value(value);
    }

    void set_biome_paletted(
        Vec3<int> pos4, std::span<const uint32_t> palette, size_t index)
    {
        m_biomes.set_paletted(pos4, palette, index);
    }

    void set_biome_direct(Vec3<int> pos4, uint8_t value)
    {
        m_biomes.set_direct(pos4, value);
    }

    auto serialize(auto it)
    {
        it = write_number(it, m_block_count);
        it = write_number(it, m_fluid_count);
        it = m_block_states.serialize(it);
        it = m_biomes.serialize(it);
        return it;
    }

private:
    void inc_dec_counts(
        uint32_t previous_block_state, uint32_t next_block_state)
    {
        bool is_previous_air = is_air(previous_block_state),
             is_previous_fluid = is_fluid(previous_block_state),
             is_next_air = is_air(next_block_state),
             is_next_fluid = is_fluid(next_block_state);

        if (is_previous_air && !is_next_air)
            --m_block_count;
        else if (!is_previous_air && is_next_air)
            ++m_block_count;
        if (is_previous_fluid && !is_next_fluid)
            --m_fluid_count;
        else if (!is_previous_fluid && is_next_fluid)
            ++m_fluid_count;
    }

private:
    PalettedContainer<16, 4, 8, 15> m_block_states;
    PalettedContainer<4, 1, 3, 7> m_biomes;
    uint16_t m_block_count { }, m_fluid_count { };
};

/******************************************************************************/

export class Chunk
{
public:
    decltype(auto) get_section_by_index(this auto &&self, size_t index)
    {
        return self.m_sections[index];
    }

    auto *get_section_by_y(this auto &&self, int y)
    {
        if (y < -64 || y >= 320)
            return nullptr;
        return &self.m_sections[(y + 64) / 16];
    }

    Chunk *get_positive_x_neighbor() { return m_positive_x_neighbor; }
    Chunk *get_negative_x_neighbor() { return m_negative_x_neighbor; }
    Chunk *get_positive_z_neighbor() { return m_positive_z_neighbor; }
    Chunk *get_negative_z_neighbor() { return m_negative_z_neighbor; }

private:
    Chunk(std::array<ChunkSection, 24> &&sections, Chunk *px, Chunk *nx,
        Chunk *pz, Chunk *nz)
        : m_sections(std::move(sections))
        , m_positive_x_neighbor(px)
        , m_negative_x_neighbor(nx)
        , m_positive_z_neighbor(pz)
        , m_negative_z_neighbor(nz)
    {
    }

private:
    std::array<ChunkSection, 24> m_sections;
    Chunk *m_positive_x_neighbor { }, *m_negative_x_neighbor { },
        *m_positive_z_neighbor { }, *m_negative_z_neighbor { };
};

/******************************************************************************/

class ChunkPool
{
};
