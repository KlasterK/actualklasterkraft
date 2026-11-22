module;
#include <boost/container/static_vector.hpp>
#include <boost/smart_ptr/make_local_shared.hpp>
#include <boost/system.hpp>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string_view>
#include <tuple>
export module actualklasterkraft.net.play.commands;

import actualklasterkraft.generic.errc;
import actualklasterkraft.generic.math;
import actualklasterkraft.data.nbtbuilder;
import actualklasterkraft.data.protocolprimitives;
import actualklasterkraft.data.unicode;
import actualklasterkraft.world.player;
import actualklasterkraft.world.posrot;

namespace sys = boost::system;
using namespace protocolprimitives;

void send_message(Player &player, std::string_view text, bool is_red)
{
    auto text_component
        = boost::make_local_shared<Player::TextComponentStorage>();

    NBTBuilder builder(std::back_inserter(*text_component));
    builder << nbttags::Compound << nbttags::String << "text" << text;
    if (is_red)
        builder << nbttags::String << "color"
                << "red";
    builder << nbttags::End;

    player.send_chat_message(text_component);
}

export constexpr uint32_t ServerboundChatCommandPacketID = 0x07;

export std::tuple<sys::error_code, std::string_view> on_chat_command_packet(
    std::span<const uint8_t> data, Player &player)
{
    sys::error_code ec;
    auto it = data.begin();
    uint32_t command_len { };

    std::tie(command_len, it, ec) = read_var<uint32_t>(it, data.end());
    if (ec)
        return { ec, "Chat Command" };

    if (command_len == 0)
        return { };
    if (it + command_len > data.end())
        return { MCProtocolError::UnsufficientPacketData, "Chat Command" };
    if (it + command_len < data.end())
        return { MCProtocolError::ExcessPacketData, "Chat Command" };

    auto [command, args] = unicode::lsplit_once(
        { static_cast<const char *>(static_cast<const void *>(&*it)),
            command_len });

    if (command == "tp")
    {
        auto [x_s, yzrest_s] = unicode::lsplit_once(args);
        auto [y_s, zrest_s] = unicode::lsplit_once(yzrest_s);
        auto [z_s, rest_s] = unicode::lsplit_once(zrest_s);
        if (!rest_s.empty())
        {
            send_message(player, "tp: excess arguments", true);
            return { };
        }

        PosRot result;
        result.is_position_present = true;
        result.force_sync = true;
        for (auto [axis, arg] : (std::pair<double &, std::string_view>[]) {
                 { result.position.x, x_s },
                 { result.position.y, y_s },
                 { result.position.z, z_s },
             })
        {
            auto result = std::from_chars(&*arg.begin(), &*arg.end(), axis);
            if (result.ec != std::errc { })
            {
                std::string_view message = "tp: unknown error";
                if (result.ec == std::errc::result_out_of_range)
                    message = "tp: result out of range";
                else if (result.ec == std::errc::invalid_argument)
                    message = "tp: invalid argument";

                send_message(player, message, true);
                return { };
            }
        }

        player.update_posrot(result);
        return { };
    }

    send_message(player, "SERVER: no such command", true);
    return { };
}
