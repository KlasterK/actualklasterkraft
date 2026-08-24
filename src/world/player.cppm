module;
#include <boost/asio.hpp>
export module actualklasterkraft.world.player;

import actualklasterkraft.bitfields;
import actualklasterkraft.pubsub;
import actualklasterkraft.world.math;

namespace asio = boost::asio;
namespace sys = boost::system;

export class Player
{
public:
    struct PosRot
    {
        Vec3<double> position;
        Angle pitch;
        Angle yaw;
        uint8_t is_on_ground : 1;
        uint8_t is_pushing_against_wall : 1;
        uint8_t is_position_present : 1;
        uint8_t is_rotation_present : 1;
    };

public:
    Player(asio::io_context &io)
        : m_posrot_signal(io)
    {
    }

    const PosRot &get_last_posrot() const { return m_last_posrot; }

    void update_posrot(PosRot posrot)
    {
        m_last_posrot = posrot;
        m_posrot_signal.emit({ }, posrot);
    }

    auto await_for_posrot_update(auto &&completion_token)
    {
        return m_posrot_signal.wait(std::move(completion_token));
    }

private:
    WeakSignal<void(sys::error_code, PosRot)> m_posrot_signal;
    PosRot m_last_posrot;
};
