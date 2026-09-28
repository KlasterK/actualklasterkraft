module;
#include <cstddef>
export module actualklasterkraft.world.posrot;

import actualklasterkraft.generic.math;

export struct PosRot
{
    Vec3<double> position;
    Angle pitch;
    Angle yaw;
    Angle head_yaw;
    bool is_on_ground : 1 = false;
    bool is_pushing_against_wall : 1 = false;
    bool is_position_present : 1 = false;
    bool is_rotation_present : 1 = false;

    void partial_update(const PosRot &other)
    {
        if (other.is_position_present)
        {
            position = other.position;
            is_position_present = true;
        }

        if (other.is_rotation_present)
        {
            pitch = other.pitch;
            yaw = other.yaw;
            head_yaw = other.head_yaw;
            is_rotation_present = true;
        }

        is_on_ground = other.is_on_ground;
        is_pushing_against_wall = other.is_pushing_against_wall;
    }
};
