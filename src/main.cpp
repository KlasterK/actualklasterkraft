#include <boost/asio.hpp>
#include <print>

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
