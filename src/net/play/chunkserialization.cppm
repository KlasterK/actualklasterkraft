module;
#include <cstddef>
#include <cstdint>
export module actualklasterkraft.net.play.chunkserialization;

import actualklasterkraft.data.protocolprimitives;
import actualklasterkraft.world.chunk;

using namespace protocolprimitives;

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
