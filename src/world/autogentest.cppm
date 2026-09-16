module;
#include <array>
#include <boost/container/static_vector.hpp>
#include <cstdint>
export module actualklasterkraft.world.autogentest;

import actualklasterkraft.protocolprimitives;
import actualklasterkraft.world.math;

using namespace protocolprimitives;

export namespace chunkgen
{
    constexpr int32_t Air = 0;
    constexpr int32_t GrassBlock = 9;
    constexpr int32_t Desert = 14;

    constexpr std::array<int32_t, 24> AirBlockStates { Air, Air, Air, Air, Air,
        Air, Air, Air, Air, Air, Air, Air, Air, Air, Air, Air, Air, Air, Air,
        Air, Air, Air, Air, Air };

    auto put_chunk_section_single_valued(auto it, int16_t block_count,
        int16_t fluid_count, int32_t block_state_value, int32_t biome_value)
    {
        it = write_integer(it, block_count);
        it = write_integer(it, fluid_count);

        // Block states paletted container
        *it++ = 0x00; // bits per entry
        it = write_var<int32_t>(it, block_state_value);

        // Biomes paletted container
        *it++ = 0x00; // bits per entry
        it = write_var<int32_t>(it, biome_value);

        return it;
    }

    auto put_no_light(auto it)
    {
        *it++ = 0; // Sky Light Mask
        *it++ = 0; // Block Light Mask
        *it++ = 0; // Empty Sky Light Mask
        *it++ = 0; // Empty Block Light Mask
        *it++ = 0; // Sky Lights Arrays
        *it++ = 0; // Block Lights Arrays
        return it;
    }

    auto single_valued_sectioned_chunk(
        Vec2<int32_t> chunk_pos, const std::array<int32_t, 24> &block_states)
    {
        boost::container::static_vector<uint8_t, 32> buf1;
        auto it1 = std::back_inserter(buf1);

        *it1++ = 0x2D; // id Chunk Data and Update Light

        it1 = write_integer(it1, chunk_pos.x);
        it1 = write_integer(it1, chunk_pos.z);

        // Heightmaps (empty)
        it1 = write_var<uint32_t>(it1, 0);

        // Data
        boost::container::static_vector<uint8_t, 512> buf2;
        auto it2 = std::back_inserter(buf2);

        // minecraft:overworld chunk height contains 24 chunk sections
        for (int32_t block_state : block_states)
            put_chunk_section_single_valued(
                it2, 4096, 4096, block_state, Desert);

        it1 = write_var<uint32_t>(it1, buf2.size());

        // Block Entities (empty)
        *it2++ = 0;

        // Light
        it2 = put_no_light(it2);

        return std::make_tuple(buf1, buf2);
    }
}
