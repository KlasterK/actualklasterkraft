#include <boost/asio.hpp>
module actualklasterkraft.world.chunk;

boost::asio::awaitable<bool> chunkimpl::load(Chunk &) { co_return false; }

boost::asio::awaitable<void> chunkimpl::store(const Chunk &) { co_return; }
