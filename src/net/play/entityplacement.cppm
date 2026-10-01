module;
#include <array>
#include <boost/asio.hpp>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <ranges>
export module actualklasterkraft.net.play.entityplacement;

import actualklasterkraft.generic.errc;
import actualklasterkraft.data.protocolprimitives;
import actualklasterkraft.world.player;
import actualklasterkraft.net.base.disconnecthelpers;
import actualklasterkraft.net.base.packetops;
import actualklasterkraft.net.base.transport;
import actualklasterkraft.net.play.session;

namespace asio = boost::asio;
using namespace protocolprimitives;

export asio::awaitable<void> send_spawn_entity_of_player(
    std::shared_ptr<PlaySession> session, Player &other)
{
    SessionTaskGuard guard { session };
    Transport &transport = session->transport;

    std::array<uint8_t, 64> buf;
    auto it = buf.data();

    *it++ = 0x01; // Spawn Entity
    it = write_var<uint32_t>(it, other.get_eid());
    it = std::ranges::copy(other.get_uuid(), it).out;
    it = write_var<uint32_t>(it, 155); // Entity Type = Player
    it = write_xyz(it, other.get_posrot().position);
    // Velocity is LpVec3, 0x00 means nought velocity
    *it++ = 0x00;
    it = write_angle256(it, other.get_posrot().pitch);
    it = write_angle256(it, other.get_posrot().yaw);
    it = write_angle256(it, other.get_posrot().head_yaw);
    // Data, not for players
    it = write_var<uint32_t>(it, 0);

    auto ec = co_await packetops::put(
        transport, asio::buffer(buf.data(), it - buf.data()));
    if (is_normal_shutdown(ec))
        co_return;
    else if (ec)
        co_await disconnect::play(
            transport, disconnect::fmt_reason(ec, "Spawn Entity"));
}

export asio::awaitable<void> send_remove_entity_of_player(
    std::shared_ptr<PlaySession> session, Player &other)
{
    SessionTaskGuard guard { session };
    Transport &transport = session->transport;

    // Remove Entities: Packet ID, count of Entity IDs
    std::array<uint8_t, 16> buf { 0x4D, 0x01 };
    auto end = write_var<uint32_t>(buf.data() + 2, other.get_eid());

    auto ec = co_await packetops::put(
        transport, asio::buffer(buf.data(), end - buf.data()));
    if (is_normal_shutdown(ec))
        co_return;
    else if (ec)
        co_await disconnect::play(
            transport, disconnect::fmt_reason(ec, "Remove Entities"));
}
