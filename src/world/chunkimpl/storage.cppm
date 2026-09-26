module;

#include <boost/asio.hpp>

import actualklasterkraft.world.chunk;

export module actualklasterkraft.world.chunk:storage;

namespace chunkimpl
{
    boost::asio::awaitable<bool> load(Chunk &) 
    { 
        co_return false; 
    }

    boost::asio::awaitable<void> store(const Chunk &) 
    { 
        co_return; 
    }
}
