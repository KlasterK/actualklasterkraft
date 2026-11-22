module;
#include <cstddef>
#include <cstdint>
#include <random>
module actualklasterkraft.world.chunk;

static std::minstd_rand g_rng { std::random_device { }() };

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

            static std::discrete_distribution dist { 20, 4, 2, 1 };
            switch (dist(g_rng))
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
            case 3:
                section.set_block_indirect({ x, 1, z },
                    blockstates::superflat::Palette,
                    blockstates::superflat::TallGrassLower);
                section.set_block_indirect({ x, 2, z },
                    blockstates::superflat::Palette,
                    blockstates::superflat::TallGrassUpper);
                break;
            }
        }
    }
}
