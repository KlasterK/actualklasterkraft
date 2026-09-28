module;
#include <cstddef>
#include <cstdint>
#include <random>
module actualklasterkraft.world.chunk;

static std::minstd_rand g_rng { std::random_device { }() };
static std::discrete_distribution g_plants_distribution { 10, 2, 1 };

void chunkimpl::generate(Chunk &chunk)
{
    auto &section = chunk.get_section_by_index(9);
    section.reset_with_palette(blockstates::superflat::Palette);
    for (int32_t x { }; x < 16; ++x)
    {
        for (int32_t z { }; z < 16; ++z)
        {
            section.set_block_indirect({ x, 0, z },
                blockstates::superflat::Palette,
                blockstates::superflat::GrassBlock);

            switch (std::discrete_distribution { 10, 2, 1 }(g_rng))
            {
            case 1:
                section.set_block_indirect({ x, 1, z },
                    blockstates::superflat::Palette,
                    blockstates::superflat::ShortGrass);
                break;
            case 2:
                section.set_block_indirect({ x, 1, z },
                    blockstates::superflat::Palette,
                    blockstates::superflat::Bush);
                break;
            }
        }
    }
}
