module;
#include <boost/asio.hpp>
export module actualklasterkraft.net.base.statecoroutines;

import actualklasterkraft.world.player;
import actualklasterkraft.net.base.transport;

export namespace statecoroutines
{
    boost::asio::awaitable<void> handshake(Transport transport);
    boost::asio::awaitable<void> status(Transport transport);
    boost::asio::awaitable<void> login(Transport transport, bool is_transfer);
    boost::asio::awaitable<void> configuration(
        Transport transport, Player::SpawnInfo collected_info);
    boost::asio::awaitable<void> play(
        Transport transport, Player::SpawnInfo collected_info);
}
