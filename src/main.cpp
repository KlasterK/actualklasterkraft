#include "acceptor.hpp"
#include <boost/asio.hpp>

int main()
{
    boost::asio::io_context io;
    Acceptor acceptor(io, 25565);
    acceptor.start();
    io.run();
    return 0;
}
