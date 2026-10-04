module;
#include <cstddef>
#include <cstdint>
#include<cmath>
#include<iostream>
#include<chrono>

export module actualklasterkraft.net.play.chunkserialization;

import actualklasterkraft.data.protocolprimitives;
import actualklasterkraft.world.chunk;

using namespace protocolprimitives;


export namespace chunkserialization
{
    constexpr int WORLD_HEIGHT = 384;

    constexpr uint64_t get_heightmap_mask(uint64_t height, int bpe, int entries_per_long) {
        uint64_t mask = 0;
        for (int slot = 0; slot < entries_per_long; ++slot) {
            mask |= (height << (slot * bpe));
        }
        return mask;
    }

    auto heightmaps(auto it, Chunk &chunk)
    {

        const uint8_t heightmap_ids[] = { 1};

        *it++ = sizeof(heightmap_ids) / sizeof(heightmap_ids[0]); // hmap count

        
        constexpr int BPE = 9; 
        constexpr int sectionsPerLong = 64 / BPE; // 7
        constexpr int longsCount = 37; // (256 + 7 - 1) / 7
        
        constexpr uint64_t targetHeight = 81 + 65;
        
        constexpr uint64_t longVal_height_81 = get_heightmap_mask(targetHeight, BPE, sectionsPerLong);

        for (uint8_t id : heightmap_ids)
        {
            *it++ = id;
            
            *it++ = 0x25; 

            for (int i = 0; i < longsCount; ++i)
            {
                it = write_number<uint64_t>(it,longVal_height_81);
            }
        }

        return it;
    }

    auto data(auto it, Chunk &chunk)
    {
        for (size_t i { }; i < Chunk::SectionCount; ++i)
        {
            it = chunk.get_section_by_index(i).net_serialize(it);
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
        *it++ = 0x01;
        it = write_number<uint64_t>(it,0b11'11111111'11111111'11111111); // all 26 sections are lit up

        *it++ = 0x00; // Block Light Mask
        *it++ = 0x00; // Empty Sky Light Mask 

        *it++ = 0x01;
        it = write_number<uint64_t>(it,0); // sections in complete darkness

        constexpr int SECTIONS_COUNT = (WORLD_HEIGHT/16) + 2;
        *it++ = SECTIONS_COUNT;
        
        for (int section = 0; section < SECTIONS_COUNT; ++section)
        {
            constexpr int LightArrSize = 2048; // 4 bits per light
            it = write_var<uint32_t>(it,LightArrSize);

            for (int i = 0; i < LightArrSize; ++i)
            {
                *it++ = 0xFF;
            }
        }

        *it++ = 0x00; // Block Light arrays 

        return it;
    }



}
