module;
#include <boost/asio.hpp>
#include <boost/asio/experimental/awaitable_operators.hpp>
#include <boost/asio/experimental/channel.hpp>
#include <boost/container/static_vector.hpp>
#include <cstddef>
#include <cstdint>
export module actualklasterkraft.net.play.motion;

import actualklasterkraft.generic.completiontokens;
import actualklasterkraft.generic.errc;
import actualklasterkraft.generic.math;
import actualklasterkraft.generic.templates;
import actualklasterkraft.data.protocolprimitives;
import actualklasterkraft.world.player;
import actualklasterkraft.world.posrot;
import actualklasterkraft.net.base.disconnecthelpers;
import actualklasterkraft.net.base.packetops;
import actualklasterkraft.net.base.transport;
import actualklasterkraft.net.play.packetrouter;

namespace asio = boost::asio;
namespace sys = boost::system;
using namespace protocolprimitives;
using namespace asio::experimental::awaitable_operators;
using ISI = std::istreambuf_iterator<char>;

constexpr uint32_t UpdateEntityPosPacketID = 0x35,
                   UpdateEntityPosRotPacketID = 0x36,
                   UpdateEntityRotPacketID = 0x38;

int16_t calculate_axis_i16_delta(double current, double previous)
{
    double result = current * 4096.0 - previous * 4096.0;
    if (result > 32767.9)
        return 32767;
    if (result < -32768.9)
        return -32768;
    return static_cast<int16_t>(result);
}

export asio::awaitable<void> pull_posrot_loop(
    Transport &transport, Player &self, Player &target)
{
    for (;;)
    {
        PosRot previous_posrot = target.get_posrot();

        auto variant = co_await (
            target.wait_posrot_update(asio::as_tuple(asio::use_awaitable))
            || self.wait_player_exit_simulation_distance(
                asio::as_tuple(asio::use_awaitable)));
        if (auto *exited = std::get_if<1>(&variant))
        {
            auto &[ec, p] = *exited;
            if (!ec && p == &target)
                co_return;
            continue;
        }
        auto [ec, partial_posrot] = std::move(std::get<0>(variant));
        if (ec == MCGameError::EntityWasKilled)
            co_return;
        else if (ec)
            co_return co_await disconnect::play(transport,
                disconnect::fmt_reason(
                    ec, "while waiting Player.OnPosRotUpdate"));

        boost::container::static_vector<uint8_t, 32> vec;
        auto it = std::back_inserter(vec);
        write_var<uint32_t>(it,
            partial_posrot.is_position_present
                ? (partial_posrot.is_rotation_present
                          ? UpdateEntityPosRotPacketID
                          : UpdateEntityPosPacketID)
                : UpdateEntityRotPacketID);
        write_var<uint32_t>(it, target.get_eid());
        if (partial_posrot.is_position_present)
        {
            write_number(it,
                calculate_axis_i16_delta(
                    partial_posrot.position.x, previous_posrot.position.x));
            write_number(it,
                calculate_axis_i16_delta(
                    partial_posrot.position.y, previous_posrot.position.y));
            write_number(it,
                calculate_axis_i16_delta(
                    partial_posrot.position.z, previous_posrot.position.z));
        }
        if (partial_posrot.is_rotation_present)
        {
            write_angle256(it, partial_posrot.yaw);
            write_angle256(it, partial_posrot.pitch);
        }
        *it++ = partial_posrot.is_on_ground;

        ec = co_await packetops::put(transport, asio::buffer(vec));
        if (is_normal_shutdown(ec))
            co_return;
        else if (ec)
            co_return co_await disconnect::play(transport,
                disconnect::fmt_reason(
                    ec, "Update Player Position/Rotation/both"));

        if (partial_posrot.is_rotation_present)
        {
            vec.clear();
            *it++ = 0x53; // Update Head Rotation
            write_var<uint32_t>(it, target.get_eid());
            write_angle256(it, partial_posrot.head_yaw);

            ec = co_await packetops::put(transport, asio::buffer(vec));
            if (is_normal_shutdown(ec))
                co_return;
            else if (ec)
                co_return co_await disconnect::play(transport,
                    disconnect::fmt_reason(ec, "Update Head Rotation"));
        }
    }
}

constexpr uint32_t SetPlayerPosPacketID = 0x1E, SetPlayerPosRotPacketID = 0x1F,
                   SetPlayerRotPacketID = 0x20,
                   SetPlayerMovementFlagsPacketID = 0x21;

export asio::awaitable<void> push_posrot_loop(Transport &transport,
    asio::streambuf &sb, PacketRouter &packet_router, Player &self)
{
    asio::experimental::channel<PacketRouter::OnPacketSignature> channel(
        transport.socket.get_executor());
    auto send_cb = [&](sys::error_code ec, uint32_t id)
    { channel.async_send(ec, id, asio::detached); };

    auto sub_pos = packet_router.subscribe(SetPlayerPosPacketID, send_cb);
    auto sub_both = packet_router.subscribe(SetPlayerPosRotPacketID, send_cb);
    auto sub_rot = packet_router.subscribe(SetPlayerRotPacketID, send_cb);
    auto sub_none
        = packet_router.subscribe(SetPlayerMovementFlagsPacketID, send_cb);

    for (;;)
    {
        auto [ec, packet_id] = co_await channel.async_receive(asio::as_tuple);
        if (is_normal_shutdown(ec))
            co_return;
        else if (ec)
            co_return co_await disconnect::play(transport,
                disconnect::fmt_reason(
                    ec, "Set Player Position/Rotation/both/Movement Flags"));

        PosRot result;
        if (packet_id == SetPlayerPosPacketID
            || packet_id == SetPlayerPosRotPacketID)
        {
            for (auto *axis : result.position.to_ptr_array())
            {
                std::tie(*axis, std::ignore, ec)
                    = read_number<double>(ISI(&sb), ISI());
                if (ec)
                    co_return co_await disconnect::play(transport,
                        disconnect::fmt_reason(ec,
                            "Set Player Position/Rotation/both/Movement Flags"));
            }
            result.is_position_present = true;
        }
        if (packet_id == SetPlayerRotPacketID
            || packet_id == SetPlayerPosRotPacketID)
        {
            for (auto *axis : { &result.yaw, &result.pitch })
            {
                *axis
                    = Angle::from_degrees(InlineTie(TieReturn, std::ignore, ec)
                        = read_number<float>(ISI(&sb), ISI()));
                if (ec)
                    co_return co_await disconnect::play(transport,
                        disconnect::fmt_reason(ec,
                            "Set Player Position/Rotation/both/Movement Flags"));
            }
            result.head_yaw = result.yaw;
            result.is_rotation_present = true;
        }
        int flags = sb.sbumpc();
        if (flags < 0)
            co_return co_await disconnect::play(transport,
                disconnect::fmt_reason(MCProtocolError::UnsufficientPacketData,
                    "Set Player Position/Rotation/both/Movement Flags"));
        if (sb.sbumpc() >= 0)
            co_return co_await disconnect::play(transport,
                disconnect::fmt_reason(MCProtocolError::ExcessPacketData,
                    "Set Player Position/Rotation/both/Movement Flags"));
        result.is_on_ground = flags & 0b01;
        result.is_pushing_against_wall = flags & 0b10;

        self.update_posrot(result);
    }
}
