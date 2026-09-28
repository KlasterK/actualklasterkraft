module;
#include <cstddef>
#include <cstdint>
export module actualklasterkraft.net.play.initiation;

import actualklasterkraft.data.protocolprimitives;
import actualklasterkraft.world.player;

using namespace protocolprimitives;

export template <typename OutputIt>
OutputIt put_login_packet(OutputIt it, Player &player)
{
    // Values are mostly copied from Notchian server
    // Packet ID
    *it++ = 0x31;
    // Entity ID
    it = write_number(it, player.get_eid());
    // Is hardcore
    *it++ = 0;
    // Present dimension names
    it = write_var<uint32_t>(it, 1); // count
    it = write_string(it, "minecraft:overworld");
    // Max players
    it = write_var<uint32_t>(it, get_global_player_pool().max_players());
    // View distance
    *it++ = player.get_view_distance();
    // Simulation distance
    *it++ = 8;
    // Reduced debug info
    *it++ = 0;
    // Enable respawn screen
    *it++ = 1;
    // Do limited crafting
    *it++ = 0;
    // Dimension type player will be spawned into
    *it++ = 0;
    // Dimension name player will be spawned into
    it = write_string(it, "minecraft:overworld");
    // First 8 bytes of seed's SHA-256
    it = write_number<uint64_t>(it, 6372804237062459062zu);
    // Gamemode
    *it++ = 1; // Creative
    // Previous gamemode
    *it++ = 0xFF; // Undefined
    // Is debug mode world (used to test resourcepacks, not our case)
    *it++ = 0;
    // Is superflat world (affects rendering)
    *it++ = 0;
    // Has death location (since disabled, death dimension name and death location fields are not present)
    *it++ = 0;
    // Portal cooldown in ticks
    *it++ = 0;
    // Sea level
    *it++ = 63;
    // Enforce secure chat
    *it++ = 0;

    return it;
}
