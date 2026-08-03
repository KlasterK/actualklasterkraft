#ifdef ACTUALKLASTERKRAFT_IMPLEMENT_STD_PRINT_TERMINAL_FUNCTIONS_WIN32
    #define WIN32_LEAN_AND_MEAN
    #include <windows.h>
#endif

#include <boost/asio.hpp>
#include <cstdio>
#include <print>
#include <system_error>

import actualklasterkraft.acceptor;

int main()
{
    std::println("Hello World!");

    boost::asio::io_context io;
    Acceptor acceptor(io, 25565);
    acceptor.start();
    io.run();

    return 0;
}

#ifdef ACTUALKLASTERKRAFT_IMPLEMENT_STD_PRINT_TERMINAL_FUNCTIONS_WIN32
    namespace std
    {
        void *__open_terminal(FILE *)
        {
            return GetStdHandle(STD_OUTPUT_HANDLE);
        }

        error_code __write_to_terminal(void *handle, span<char> str)
        {
            if(!WriteFile(handle, str.data(), str.size(), nullptr, nullptr))
                return std::error_code(GetLastError(), std::generic_category());
            return {};
        }
    }
#endif
