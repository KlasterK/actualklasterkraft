module;
#include <boost/asio.hpp>
#include <boost/container/static_vector.hpp>
#include <boost/intrusive_ptr.hpp>
#include <boost/smart_ptr/intrusive_ref_counter.hpp>
#include <cstdint>
#include <print>
export module actualklasterkraft.world.autogentest;

import actualklasterkraft.session;
import actualklasterkraft.streambufops;

namespace asio = boost::asio;
namespace sys = boost::system;

export namespace chunkgen
{
    constexpr int32_t Air = 0;
    constexpr int32_t GrassBlock = 9;
    constexpr int32_t Desert = 14;

    template <typename It>
    It put_chunk_section_single_valued(It it, int16_t block_count,
        int16_t fluid_count, int32_t block_state_value, int32_t biome_value)
    {
        it = protocoltypes::write_integer(it, block_count);
        it = protocoltypes::write_integer(it, fluid_count);

        // Block states paletted container
        *it++ = 0x00; // bits per entry
        it = protocoltypes::write_v32(it, block_state_value);

        // Biomes paletted container
        *it++ = 0x00; // bits per entry
        it = protocoltypes::write_v32(it, biome_value);

        return it;
    }

    void put_no_light(asio::streambuf &sb)
    {
        sb.sputc(0); // Sky Light Mask
        sb.sputc(0); // Block Light Mask
        sb.sputc(0); // Empty Sky Light Mask
        sb.sputc(0); // Empty Block Light Mask
        sb.sputc(0); // Sky Lights Arrays
        sb.sputc(0); // Block Lights Arrays
    }

    void put_single_valued_sectioned_chunk(asio::streambuf &sb, int32_t chunk_x,
        int32_t chunk_z, std::span<const int32_t, 24> block_states)
    {
        sb.sputc(0x2D); // id Chunk Data and Update Light

        streambufops::write_integer(sb, chunk_x);
        streambufops::write_integer(sb, chunk_z);

        // Heightmaps (empty)
        streambufops::write_v32(sb, 0);

        // Data
        boost::container::static_vector<uint8_t, 512> buf;
        auto it = std::back_inserter(buf);

        // minecraft:overworld chunk height contains 24 chunk sections
        for (int32_t block_state : block_states)
            put_chunk_section_single_valued(it, 4096, 4096, block_state, Desert);

        streambufops::write_v32(sb, buf.size());
        sb.sputn(reinterpret_cast<const char *>(buf.data()), buf.size());

        // Block Entities (empty)
        sb.sputc(0);

        // Light
        put_no_light(sb);
    }

    void put_empty_chunk(asio::streambuf &sb, int32_t chunk_x, int32_t chunk_z)
    {
        std::array<int32_t, 24> block_states;
        block_states.fill(Air);
        put_single_valued_sectioned_chunk(sb, chunk_x, chunk_z, block_states);
    }
}
