#include <random>
#ifdef ACTUALKLASTERKRAFT_IMPLEMENT_STD_PRINT_TERMINAL_FUNCTIONS_WIN32
#define WIN32_LEAN_AND_MEAN
#include <cstdio>
#include <system_error>
#include <windows.h>
#endif

#include <boost/asio.hpp>
#include <print>

import actualklasterkraft.acceptor;
import actualklasterkraft.world.blockstates;
import actualklasterkraft.world.chunk;
import actualklasterkraft.world.math;
import actualklasterkraft.world.player;

void prepare_world()
{
    Chunk *central_chunk = get_global_chunk_pool().acquire({ 0, 0 });
    for (int32_t x = -1; x <= 1; ++x)
    {
        for (int32_t z = -1; z <= 1; ++z)
        {
            if (x == 0 && z == 0)
                continue;
            (void)get_global_chunk_pool().acquire({ x, z });
        }
    }

    auto &section = central_chunk->get_section_by_index(9);
    section.reset_with_palette(blockstates::superflat::Palette);
    for (int32_t x { }; x < 16; ++x)
    {
        for (int32_t z { }; z < 16; ++z)
        {
            section.set_block_indirect({ x, 0, z },
                blockstates::superflat::Palette,
                blockstates::superflat::GrassBlock);

            static std::mt19937 rng { std::random_device { }() };
            static std::discrete_distribution dist { 10, 2, 1 };

            switch (dist(rng))
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

int main()
{
    std::println("Hello World!");

    prepare_world();
    boost::asio::io_context io;

    Acceptor acceptor(io.get_executor(), 25565);
    acceptor.start();

    io.run();
    return 0;
}

#ifdef ACTUALKLASTERKRAFT_IMPLEMENT_STD_PRINT_TERMINAL_FUNCTIONS_WIN32
namespace std
{
    void *__open_terminal(FILE *) { return GetStdHandle(STD_OUTPUT_HANDLE); }

    error_code __write_to_terminal(void *handle, span<char> str)
    {
        if (!WriteFile(handle, str.data(), str.size(), nullptr, nullptr))
            return std::error_code(GetLastError(), std::generic_category());
        return { };
    }
}
#endif
