module;
#include <boost/asio.hpp>
#include <boost/asio/experimental/channel.hpp>
export module actualklasterkraft.world.player;

import actualklasterkraft.bitfields;
import actualklasterkraft.world.math;

namespace asio = boost::asio;
namespace sys = boost::system;
namespace asiox = asio::experimental;

export class Player
{
public:
    Vec3<double> get_position() const { return m_position; }
    Vec3<double> get_velocity() const { return m_velocity; }
    Angle get_pitch() const { return m_pitch; }
    Angle get_yaw() const { return m_yaw; }

    void update_position_and_rotation(Vec3<double> position, Angle pitch,
        Angle yaw, PositionAndRotationFlags::IntT flags)
    {
        m_updposrot_channel.async_receive()
    }

private:
    Vec3<double> m_position, m_velocity;
    Angle m_pitch, m_yaw;

    asiox::channel<void(sys::error_code, Vec3<double> position)>
        m_updposrot_channel;
};
