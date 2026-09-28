#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#endif

#include <boost/asio.hpp>
#include <cstdio>
#include <print>
#include <system_error>

import actualklasterkraft.generic.completiontokens;
import actualklasterkraft.generic.templates;
import actualklasterkraft.world.chunk;
import actualklasterkraft.net.base.acceptor;

boost::asio::awaitable<void> init(Acceptor &acceptor)
{
    // Prepare spawn area
    for (int32_t x = -3; x <= 3; ++x)
    {
        for (int32_t z = -3; z <= 3; ++z)
        {
            (void)co_await get_global_chunk_pool().acquire({ x, z });
        }
    }

    acceptor.start();
}

int main()
{
    std::println("Hello World!");

    boost::asio::io_context io;

    Acceptor acceptor(io.get_executor(), 25565);
    boost::asio::co_spawn(
        io, init(acceptor), detached_rethrow_exceptions_token);

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
