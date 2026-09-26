module;
#include <algorithm>
#include <array>
#include <boost/asio.hpp>
#include <boost/container/flat_map.hpp>
#include <cassert>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <memory>
#include <ranges>
#include <span>
#include <variant>
export module actualklasterkraft.world.chunk;

import actualklasterkraft.basepool;
import actualklasterkraft.protocolprimitives;
import actualklasterkraft.world.blockstates;
import actualklasterkraft.world.math;

namespace asio = boost::asio;
using namespace protocolprimitives;

/******************************************************************************/

auto encode_bit_entries(auto it, size_t bits_per_entry, auto get_value)
{
    size_t entries_per_u64 = 64 / bits_per_entry;
    size_t u64s_count
        = (BlockExtentCub + entries_per_u64 - 1) / entries_per_u64;
    for (size_t i { }; i < u64s_count; ++i)
    {
        uint64_t storage { };
        for (size_t j { entries_per_u64 }; j-- > 0;)
        {
            if (i * entries_per_u64 + j >= BlockExtentCub)
                continue;
            storage <<= bits_per_entry;
            storage |= get_value(i * entries_per_u64 + j);
        }
        it = write_number(it, storage);
    }
    return it;
}

struct DirectPC
{
    std::unique_ptr<std::array<uint16_t, BlockExtentCub>> data;

    DirectPC()
        : data(std::make_unique<std::array<uint16_t, BlockExtentCub>>())
    {
    }

    uint32_t get(size_t idx) const { return (*data)[idx]; }

    auto serialize(auto it) const
    {
        it = write_number(it, uint8_t(15));
        it = encode_bit_entries(
            it, 15, [this](size_t idx) { return (*data)[idx]; });
        return it;
    }
};

struct SingleValuedPC
{
    uint32_t value = 0;

    uint32_t get(size_t) const { return value; }

    auto serialize(auto it) const
    {
        it = write_number(it, uint8_t(0));
        it = write_var<uint32_t>(it, value);
        return it;
    }
};

struct IndirectPC
{
    std::span<const uint32_t> palette;
    std::unique_ptr<uint8_t[]> data;

    IndirectPC(std::span<const uint32_t> palette)
        : palette(palette)
        , data(std::make_unique<uint8_t[]>(
              palette.size() < 16 ? BlockExtentCub / 2 : BlockExtentCub))
    {
        assert(palette.size() < 256);
    }

    uint32_t get(size_t idx) const
    {
        if (palette.size() >= 16)
            return palette[data[idx]];
        if (idx % 2 == 0)
            return palette[data[idx / 2] & 0x0F];
        return palette[data[idx / 2] >> 4];
    }

    auto serialize(auto it) const
    {
        uint8_t bpe = std::max(4, std::bit_width(palette.size() - 1));

        it = write_number<uint8_t>(it, bpe);
        it = write_var<uint32_t>(it, palette.size());
        for (uint32_t entry : palette)
            it = write_var<uint32_t>(it, entry);
        it = encode_bit_entries(it, bpe,
            [this](size_t idx) -> uint8_t
            {
                if (palette.size() >= 16)
                    return data[idx];
                if (idx % 2 == 0)
                    return data[idx / 2] & 0x0F;
                return data[idx / 2] >> 4;
            });

        return it;
    }

    void set(size_t idx, uint8_t palette_idx)
    {
        if (palette.size() >= 16)
            data[idx] = palette_idx;
        if (idx % 2 == 0)
            data[idx / 2] = (data[idx / 2] & 0xF0) | palette_idx;
        else
            data[idx / 2] = (data[idx / 2] & 0x0F) | (palette_idx << 4);
    }

    bool test_palette(std::span<const uint32_t> other) const
    {
        return palette.data() == other.data();
    }
};

using PalettedContainer = std::variant<DirectPC, SingleValuedPC, IndirectPC>;

/******************************************************************************/

export class ChunkSection
{
public:
    ChunkSection()
        : m_block_states(SingleValuedPC { 0 })
    {
    }

    ChunkSection(std::span<const uint32_t> palette)
        : m_block_states(IndirectPC { palette })
        , m_block_count(blockstates::is_air(palette[0]) ? 0 : BlockExtentCub)
        , m_fluid_count(blockstates::is_fluid(palette[0]) ? BlockExtentCub : 0)
    {
    }

    void reset_with_palette(std::span<const uint32_t> palette)
    {
        *this = ChunkSection(palette);
    }

    [[nodiscard]] uint32_t get_block(Vec3<int> pos16) const
    {
        return std::visit(
            [&](const auto &pc)
            {
                return pc.get(
                    pos16.x + pos16.z * BlockExtent + pos16.y * BlockExtentSq);
            },
            m_block_states);
    }

    void set_block_single_value(uint32_t value)
    {
        m_block_states = SingleValuedPC { value };
        m_block_count = blockstates::is_air(value) ? 0 : BlockExtentCub;
        m_fluid_count = blockstates::is_fluid(value) ? BlockExtentCub : 0;
    }

    void set_block_indirect(
        Vec3<int> pos16, std::span<const uint32_t> palette, size_t palette_idx)
    {
        if (auto *indirect = std::get_if<IndirectPC>(&m_block_states);
            indirect && indirect->test_palette(palette))
        {
            inc_dec_counts(get_block(pos16), palette[palette_idx]);
            indirect->set(
                pos16.x + pos16.z * BlockExtent + pos16.y * BlockExtentSq,
                palette_idx);
            return;
        }
        set_block_direct(pos16, palette[palette_idx]);
    }

    void set_block_direct(Vec3<int> pos16, uint16_t value)
    {
        inc_dec_counts(get_block(pos16), value);
        (*convert_to_direct().data)[pos16.x + pos16.z * BlockExtent
            + pos16.y * BlockExtentSq] = value;
    }

    auto serialize(auto it)
    {
        it = write_number(it, m_block_count);
        it = write_number(it, m_fluid_count);
        std::visit(
            [&](const auto &pc) { it = pc.serialize(it); }, m_block_states);
        // TODO: implement biomes changing
        it = write_number<uint8_t>(it, 0x00); // Single Valued
        it = write_var<uint32_t>(it, 40); // hardcoded plains
        return it;
    }

private:
    DirectPC &convert_to_direct()
    {
        if (auto *indirect = std::get_if<IndirectPC>(&m_block_states))
        {
            DirectPC direct;
            for (size_t i { }; i < BlockExtentCub; ++i)
                (*direct.data)[i] = indirect->get(i);
            m_block_states = std::move(direct);
        }
        else if (auto *sv = std::get_if<SingleValuedPC>(&m_block_states))
        {
            DirectPC direct;
            for (size_t i { }; i < BlockExtentCub; ++i)
                (*direct.data)[i] = sv->value;
            m_block_states = std::move(direct);
        }
        return std::get<DirectPC>(m_block_states);
    }

    void inc_dec_counts(
        uint32_t previous_block_state, uint32_t next_block_state)
    {
        bool is_previous_air = blockstates::is_air(previous_block_state),
             is_previous_fluid = blockstates::is_fluid(previous_block_state),
             is_next_air = blockstates::is_air(next_block_state),
             is_next_fluid = blockstates::is_fluid(next_block_state);

        if (is_previous_air && !is_next_air)
            ++m_block_count;
        else if (!is_previous_air && is_next_air)
            --m_block_count;

        if (is_previous_fluid && !is_next_fluid)
            --m_fluid_count;
        else if (!is_previous_fluid && is_next_fluid)
            ++m_fluid_count;
    }

private:
    PalettedContainer m_block_states;
    uint16_t m_block_count { }, m_fluid_count { };
};

/******************************************************************************/

export class Chunk
{
public:
    static constexpr size_t SectionCount = 24;

public:
    [[nodiscard]] ChunkSection &get_section_by_index(size_t index)
    {
        return m_sections[index];
    }

    [[nodiscard]] const ChunkSection &get_section_by_index(size_t index) const
    {
        return m_sections[index];
    }

    [[nodiscard]] ChunkSection *get_section_by_block_y(int y)
    {
        if (y < -64 || y >= 320)
            return nullptr;
        return &m_sections[(y + 64) / 16];
    }

    [[nodiscard]] const ChunkSection *get_section_by_block_y(int y) const
    {
        if (y < -64 || y >= 320)
            return nullptr;
        return &m_sections[(y + 64) / 16];
    }

    [[nodiscard]] const auto &get_xz() { return m_pos; }

#define KK_CHUNK_NEIGHBOR(name_mid)                                            \
    [[nodiscard]] Chunk *get_##name_mid##_neighbor()                           \
    {                                                                          \
        return m_##name_mid##_neighbor;                                        \
    }                                                                          \
    [[nodiscard]] const Chunk *get_##name_mid##_neighbor() const               \
    {                                                                          \
        return m_##name_mid##_neighbor;                                        \
    }
    KK_CHUNK_NEIGHBOR(positive_x)
    KK_CHUNK_NEIGHBOR(negative_x)
    KK_CHUNK_NEIGHBOR(positive_z)
    KK_CHUNK_NEIGHBOR(negative_z)
#undef KK_CHUNK_NEIGHBOR

private:
    template <size_t N> friend class ChunkPool;

    Chunk() = default;

    void reset()
    {
        for (auto &section : m_sections)
            section.set_block_single_value(0);

        if (m_positive_x_neighbor)
            m_positive_x_neighbor->m_negative_x_neighbor = nullptr;

        if (m_negative_x_neighbor)
            m_negative_x_neighbor->m_positive_x_neighbor = nullptr;

        if (m_positive_z_neighbor)
            m_positive_z_neighbor->m_negative_z_neighbor = nullptr;

        if (m_negative_z_neighbor)
            m_negative_z_neighbor->m_positive_z_neighbor = nullptr;

        m_refs = 0;
        m_pos = { };
    }

private:
    std::array<ChunkSection, SectionCount> m_sections;
    Chunk *m_positive_x_neighbor { }, *m_negative_x_neighbor { },
        *m_positive_z_neighbor { }, *m_negative_z_neighbor { };
    size_t m_refs { };
    Vec2<int32_t> m_pos { };
};

/******************************************************************************/

// These functions are used by ChunkPool but their definition is placed in
// separated implementation units.
namespace chunkimpl
{
    void generate(Chunk &);
    asio::awaitable<bool> load(Chunk &);
    asio::awaitable<void> store(const Chunk &);
}

struct XFastZSlowCompare
{
    [[nodiscard]] bool operator()(
        Vec2<int32_t> lhs, Vec2<int32_t> rhs) const noexcept
    {
        return lhs.z < rhs.z || (lhs.z == rhs.z && lhs.x < rhs.x);
    }
};

using PosToChunkMap = boost::container::flat_map<Vec2<int32_t>,
    std::reference_wrapper<Chunk>, XFastZSlowCompare>;

constexpr auto lower_bound_map_cmp =
    [](PosToChunkMap::const_reference pair, const PosToChunkMap::key_type &key)
{ return XFastZSlowCompare()(pair.first, key); };

export template <size_t N> class ChunkPool : private BasePool<N>
{
private:
    using ValueType = Chunk;
    friend PoolTakenSlotsIterator<const ChunkPool>;
    Chunk &pool_iterator_dereference(size_t idx) const { return m_chunks[idx]; }
    bool pool_iterator_test(size_t) const { return true; }

public:
    ChunkPool()
        : m_chunks(new Chunk[N])
    {
    }

    [[nodiscard]] Chunk *get(Vec2<int32_t> pos) const
    {
        auto it = m_pos_to_chunk_map.find(pos);
        if (it == m_pos_to_chunk_map.end())
            return nullptr;
        return &it->second.get();
    }

    [[nodiscard]] asio::awaitable<Chunk *> acquire(Vec2<int32_t> pos)
    {
        auto it = m_pos_to_chunk_map.find(pos);
        if (it != m_pos_to_chunk_map.end())
        {
            ++it->second.get().m_refs;
            co_return &it->second.get();
        }

        size_t idx = BasePool<N>::allocate();
        if (idx == N)
            co_return nullptr;

        auto &chunk = m_chunks[idx];
        it = m_pos_to_chunk_map.emplace(pos, std::ref(chunk)).first;

        if (it != m_pos_to_chunk_map.begin())
        {
            auto prev_it = std::prev(it);
            if (prev_it->first.x == pos.x - 1 && prev_it->first.z == pos.z)
            {
                chunk.m_negative_x_neighbor = &prev_it->second.get();
                prev_it->second.get().m_positive_x_neighbor = &chunk;
            }

            auto nz_it = std::lower_bound(m_pos_to_chunk_map.begin(), it,
                Vec2(pos.x, pos.z - 1), lower_bound_map_cmp);
            if (nz_it != it)
            {
                chunk.m_negative_z_neighbor = &nz_it->second.get();
                nz_it->second.get().m_positive_z_neighbor = &chunk;
            }
        }

        auto next_it = std::next(it);
        if (next_it != m_pos_to_chunk_map.end() && next_it->first.x == pos.x + 1
            && next_it->first.z == pos.z)
        {
            chunk.m_positive_x_neighbor = &next_it->second.get();
            next_it->second.get().m_negative_x_neighbor = &chunk;
        }

        auto pz_it = std::lower_bound(next_it, m_pos_to_chunk_map.end(),
            Vec2(pos.x, pos.z + 1), lower_bound_map_cmp);
        if (pz_it != m_pos_to_chunk_map.end())
        {
            chunk.m_positive_z_neighbor = &pz_it->second.get();
            pz_it->second.get().m_negative_z_neighbor = &chunk;
        }

        chunk.m_refs = 1;
        chunk.m_pos = pos;

        if (!co_await chunkimpl::load(chunk))
            chunkimpl::generate(chunk);

        co_return &chunk;
    }

    void acquire(Chunk &chunk)
    {
        assert(&chunk >= m_chunks.get() && &chunk < m_chunks.get() + N
            && chunk.m_refs != 0);
        ++chunk.m_refs;
    }

    void release(Chunk &chunk)
    {
        assert(&chunk >= m_chunks.get() && &chunk < m_chunks.get() + N
            && chunk.m_refs != 0);

        if (--chunk.m_refs == 0)
        {
            BasePool<N>::free(&chunk - m_chunks.get());
            m_pos_to_chunk_map.erase(chunk.m_pos);
            chunk.reset();
        }
    }

    asio::awaitable<void> store_all() const
    {
        PoolTakenSlotsIterator<const ChunkPool> it { *this, 0 },
            end { *this, N };
        for (; it != end; ++it)
        {
            co_await chunkimpl::store(*it);
        }
    }

private:
    std::unique_ptr<Chunk[]> m_chunks;
    PosToChunkMap m_pos_to_chunk_map;
};

/******************************************************************************/

template class ChunkPool<16384>;

export [[nodiscard]] auto &get_global_chunk_pool()
{
    static ChunkPool<16384> pool;
    return pool;
}

/******************************************************************************/

export namespace chunkserialization
{
    auto heightmaps(auto it, Chunk &)
    {
        // TODO: implement proper heightmaps serialisation
        return write_var<uint32_t>(it, 0);
    }

    auto data(auto it, Chunk &chunk)
    {
        for (size_t i { }; i < Chunk::SectionCount; ++i)
        {
            it = chunk.get_section_by_index(i).serialize(it);
        }
        return it;
    }

    auto block_entities(auto it, Chunk &)
    {
        // TODO: when block entities will be added, add their serialisation
        return write_var<uint32_t>(it, 0);
    }

    auto light(auto it, Chunk &)
    {
        // TODO: when light will be added, add its serialisation
        *it++ = 0; // Sky Light Mask
        *it++ = 0; // Block Light Mask
        *it++ = 0; // Empty Sky Light Mask
        *it++ = 0; // Empty Block Light Mask
        *it++ = 0; // Sky Lights Arrays
        *it++ = 0; // Block Lights Arrays
        return it;
    }
}
