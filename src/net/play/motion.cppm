module;
#include <boost/asio.hpp>
#include <boost/asio/experimental/awaitable_operators.hpp>
#include <boost/asio/experimental/channel.hpp>
#include <boost/container/static_vector.hpp>
#include <cstddef>
#include <cstdint>
#include <random>
#include <span>
export module actualklasterkraft.net.play.motion;

import actualklasterkraft.generic.completiontokens;
import actualklasterkraft.generic.errc;
import actualklasterkraft.generic.math;
import actualklasterkraft.generic.templates;
import actualklasterkraft.data.bitfields;
import actualklasterkraft.data.protocolprimitives;
import actualklasterkraft.world.player;
import actualklasterkraft.world.posrot;
import actualklasterkraft.net.base.packetops;
import actualklasterkraft.net.base.transport;
import actualklasterkraft.net.play.finalisation;

namespace asio = boost::asio;
namespace sys = boost::system;
using namespace protocolprimitives;
using namespace asio::experimental::awaitable_operators;
using PacketVec = boost::container::static_vector<uint8_t, 128>;
static constexpr auto AwTuple = asio::as_tuple(asio::use_awaitable);

constexpr double SimpleUpdateLimit = 7.9;

constexpr uint32_t UpdateEntityPosPacketID
    = 0x35,
    UpdateEntityPosRotPacketID = 0x36, UpdateEntityRotPacketID = 0x38,
    UpdateHeadRotationPacketID = 0x53, SynchronisePlayerPositionPacketID = 0x48,
    TeleportEntityPacketID = 0x23;

export constexpr uint32_t SetPlayerPosPacketID
    = 0x1E,
    SetPlayerPosRotPacketID = 0x1F, SetPlayerRotPacketID = 0x20,
    SetPlayerMovementFlagsPacketID = 0x21, ConfirmTeleportationPacketID = 0x00;

static std::minstd_rand g_rng { std::random_device { }() };

constexpr int16_t calculate_axis_i16_delta(double current, double previous)
{
    double result = current * 4096.0 - previous * 4096.0;
    return static_cast<int16_t>(result);
}

void compose_simple_update_packet(PacketVec &vec, const PosRot &previous_posrot,
    const PosRot &partial_posrot, const Player &observee)
{
    auto it = std::back_inserter(vec);
    write_var<uint32_t>(it,
        partial_posrot.is_position_present
            ? (partial_posrot.is_rotation_present ? UpdateEntityPosRotPacketID
                                                  : UpdateEntityPosPacketID)
            : UpdateEntityRotPacketID);
    write_var<uint32_t>(it, observee.get_eid());
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
}

void compose_teleport_entity_packet(PacketVec &vec, const Player &observee)
{
    auto it = std::back_inserter(vec);
    write_var<uint32_t>(it, TeleportEntityPacketID); // Teleport Entity
    write_var<uint32_t>(it, observee.get_eid());
    write_xyz(it, observee.get_posrot().position);
    write_xyz(it, Vec3<double>()); // Velocity
    write_number(it, observee.get_posrot().yaw.as_degrees());
    write_number(it, observee.get_posrot().pitch.as_degrees());
    write_number<uint8_t>(it, observee.get_posrot().is_on_ground);
}

export asio::awaitable<void> detached_pull_posrot_loop(Transport &transport,
    Player &self, Player &observee, FinalisationSignal &fin)
{
    for (;;)
    {
        PosRot previous_posrot = observee.get_posrot();

        auto opt_variant = co_await awaitable_or_fin(fin,
            observee.wait_posrot_update(AwTuple)
                || self.wait_player_exit_simulation_distance(AwTuple));
        if (!opt_variant)
            co_return;

        if (auto *exited = std::get_if<1>(&*opt_variant))
        {
            auto &[ec, p] = *exited;
            if (ec)
                co_return;
            if (p == &observee)
                co_return;
            continue;
        }

        auto [ec, partial_posrot] = std::move(std::get<0>(*opt_variant));
        if (ec == MCGameError::EntityWasKilled)
            co_return;
        if (ec)
            co_return fin.emit(ec,
                "detached_pull_posrot_loop awaiting Player::wait_posrot_update");

        if (partial_posrot.is_position_present)
        {
            auto signed_deltas
                = partial_posrot.position - previous_posrot.position;
            for (double axis : signed_deltas.to_array())
            {
                if (std::abs(axis) > SimpleUpdateLimit)
                {
                    partial_posrot.force_sync = true;
                    break;
                }
            }
        }

        PacketVec packet_vec;
        if (partial_posrot.force_sync)
            compose_teleport_entity_packet(packet_vec, observee);
        else
            compose_simple_update_packet(
                packet_vec, previous_posrot, partial_posrot, observee);

        auto opt_ec = co_await awaitable_or_fin(
            fin, packetops::put(transport, asio::buffer(packet_vec)));
        if (!opt_ec)
            co_return;
        if (*opt_ec)
            co_return fin.emit(ec, "Update Player Position/Rotation/both");

        if (partial_posrot.is_rotation_present)
        {
            packet_vec.clear();
            auto it = std::back_inserter(packet_vec);
            write_var<uint32_t>(it, UpdateHeadRotationPacketID);
            write_var<uint32_t>(it, observee.get_eid());
            write_angle256(it, partial_posrot.head_yaw);

            opt_ec = co_await awaitable_or_fin(
                fin, packetops::put(transport, asio::buffer(packet_vec)));
            if (!opt_ec)
                co_return;
            if (*opt_ec)
                co_return fin.emit(ec, "Update Head Rotation");
        }
    }
}

export class PlayerPosRotUpdater
{
public:
    std::tuple<sys::error_code, std::string_view> on_posrot_packet(
        uint32_t packet_id, std::span<const uint8_t> data, Player &self)
    {
        auto it = data.begin();
        sys::error_code ec;

        if (m_current_teleport_id)
        {
            if (packet_id != ConfirmTeleportationPacketID)
                return { };

            uint32_t teleport_id = InlineTie(TieReturn, it, ec)
                = read_var<uint32_t>(it, data.end());
            if (ec)
                return { ec, "Confirm Teleportation" };

            if (teleport_id != m_current_teleport_id)
                return { MCProtocolError::CorrelationIDMismatch,
                    "ConfirmTeleportation" };

            m_current_teleport_id = 0;
            return { };
        }

        if (packet_id == ConfirmTeleportationPacketID)
            return { };

        PosRot result;

        if (packet_id == SetPlayerPosPacketID
            || packet_id == SetPlayerPosRotPacketID)
        {
            for (auto *axis : result.position.to_ptr_array())
            {
                std::tie(*axis, it, ec) = read_number<double>(it, data.end());
                if (ec)
                    return { ec,
                        "Set Player Position/Rotation/both/Movement Flags" };
            }
            result.is_position_present = true;
        }

        if (packet_id == SetPlayerRotPacketID
            || packet_id == SetPlayerPosRotPacketID)
        {
            for (auto *axis : { &result.yaw, &result.pitch })
            {
                *axis = Angle::from_degrees(InlineTie(TieReturn, it, ec)
                    = read_number<float>(it, data.end()));
                if (ec)
                    return { ec,
                        "Set Player Position/Rotation/both/Movement Flags" };
            }
            result.head_yaw = result.yaw;
            result.is_rotation_present = true;
        }

        auto flags = InlineTie(TieReturn, it, ec)
            = read_number<uint8_t>(it, data.end());
        if (ec)
            return { ec, "Set Player Position/Rotation/both/Movement Flags" };
        if (it != data.end())
            return { MCProtocolError::ExcessPacketData,
                "Set Player Position/Rotation/both/Movement Flags" };

        result.is_on_ground = flags & 0b01;
        result.is_pushing_against_wall = flags & 0b10;

        self.update_posrot(result);
        return { };
    }

    asio::awaitable<void> detached_sync_loop(
        Transport &transport, Player &self, FinalisationSignal &fin)
    {
        for (;;)
        {
            PosRot previous_posrot = self.get_posrot();

            auto opt_ec_posrot = co_await awaitable_or_fin(
                fin, self.wait_posrot_update(AwTuple));
            if (!opt_ec_posrot)
                co_return;

            auto &[ec, partial_posrot] = *opt_ec_posrot;
            if (ec)
                co_return fin.emit(ec,
                    "PlayerPosRotUpdater::detached_sync_loop awaiting Player::wait_posrot_update");

            if (partial_posrot.is_position_present)
            {
                auto signed_deltas
                    = partial_posrot.position - previous_posrot.position;
                for (double axis : signed_deltas.to_array())
                {
                    if (std::abs(axis) > SimpleUpdateLimit)
                    {
                        partial_posrot.force_sync = true;
                        break;
                    }
                }
            }

            if (!partial_posrot.force_sync)
                continue;

            while (m_current_teleport_id)
            {
                auto opt = co_await awaitable_or_fin(fin, asio::post(AwTuple));
                if (opt)
                    co_return;
            }

            m_current_teleport_id = g_rng();
            if (m_current_teleport_id == 0)
                m_current_teleport_id = 1234;

            TeleportFlags::IntT teleport_flags { };

            PacketVec packet_vec;
            auto it = std::back_inserter(packet_vec);
            write_var<uint32_t>(it, SynchronisePlayerPositionPacketID);
            write_var<uint32_t>(it, m_current_teleport_id);
            write_xyz(it, self.get_posrot().position);
            write_xyz(it, Vec3<double>()); // Velocity
            write_number(it, self.get_posrot().yaw.as_degrees());
            write_number(it, self.get_posrot().pitch.as_degrees());
            write_number(it, teleport_flags);

            auto opt_ec = co_await awaitable_or_fin(
                fin, packetops::put(transport, asio::buffer(packet_vec)));
            if (!opt_ec)
                co_return;
            if (*opt_ec)
                co_return fin.emit(ec, "Synchronise Player Position");
        }
    }

private:
    uint32_t m_current_teleport_id { };
};
