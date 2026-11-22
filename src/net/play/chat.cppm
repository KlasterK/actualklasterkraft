module;
#include <array>
#include <boost/asio.hpp>
#include <boost/asio/experimental/awaitable_operators.hpp>
#include <boost/container/small_vector.hpp>
#include <boost/smart_ptr/make_local_shared.hpp>
#include <iterator>
#include <new>
#include <print>
#include <string_view>
export module actualklasterkraft.net.play.chat;

import actualklasterkraft.generic.completiontokens;
import actualklasterkraft.generic.errc;
import actualklasterkraft.generic.pubsub;
import actualklasterkraft.generic.templates;
import actualklasterkraft.data.nbtbuilder;
import actualklasterkraft.data.protocolprimitives;
import actualklasterkraft.data.unicode;
import actualklasterkraft.world.player;
import actualklasterkraft.net.base.disconnecthelpers;
import actualklasterkraft.net.base.packetops;
import actualklasterkraft.net.base.transport;
import actualklasterkraft.net.play.finalisation;

namespace asio = boost::asio;
namespace sys = boost::system;
using namespace protocolprimitives;
using namespace asio::experimental::awaitable_operators;

constexpr auto AwTuple = asio::as_tuple(asio::use_awaitable);
export constexpr uint32_t ServerboundChatMessagePacketID = 0x09;

export void send_usual_message_to_all(std::string_view message)
{
    std::println("{}", message);

    auto text_component
        = boost::make_local_shared<Player::TextComponentStorage>();
    NBTBuilder(std::back_inserter(*text_component))
        << nbttags::String << message;

    for (Player &player : get_global_player_pool().living_players())
    {
        player.send_chat_message(text_component);
    }
}

export void send_yellow_message_to_all(std::string_view message)
{
    std::println("{}", message);

    auto text_component
        = boost::make_local_shared<Player::TextComponentStorage>();
    NBTBuilder(std::back_inserter(*text_component))
        << nbttags::Compound << nbttags::String << "text" << message
        << nbttags::String << "color" << "yellow" << nbttags::End;

    for (Player &player : get_global_player_pool().living_players())
    {
        player.send_chat_message(text_component);
    }
}

export std::tuple<sys::error_code, std::string_view> on_chat_message_packet(
    std::span<const uint8_t> data, Player &player)
{
    sys::error_code ec;
    auto data_it = data.begin();
    uint32_t message_len { };

    std::tie(message_len, data_it, ec)
        = read_var<uint32_t>(data_it, data.end());
    if (ec)
        return { ec, "Chat Message" };
    if (message_len == 0) // it's 0 on errors too
        return { MCGameError::Kick,
            "You can't send an empty message. Don't abuse us." };
    if (data.end() - data_it < message_len)
        return { MCProtocolError::UnsufficientPacketData, "Chat Message" };

    std::string_view message_text { static_cast<const char *>(
                                        static_cast<const void *>(&*data_it)),
        message_len };

    auto opt_len = unicode::verify_count_length(message_text);
    if (!opt_len)
        return { MCGameError::Kick,
            "You can't send malformed Unicode. Don't abuse us." };
    if (*opt_len > 256)
        return {
            MCGameError::Kick,
            "You can't send a message longer than 256 characters. Don't abuse us."
        };

    std::array<char, 512> buf;
    auto buf_it = buf.data();
    *buf_it++ = '<';
    buf_it = std::ranges::copy(player.get_name(), buf_it).out;
    *buf_it++ = '>';
    *buf_it++ = ' ';
    buf_it = std::copy_n(data_it, message_len, buf_it);

    // Ignore Mojang crypto shit part of the packet (there're a lot more fields)
    // TODO: though, validating them would be good

    send_usual_message_to_all(std::string_view(buf.data(), buf_it));
    return { };
}

export asio::awaitable<void> detached_pull_chat_messages_loop(
    Transport &transport, Player &player, FinalisationSignal &fin)
{
    for (;;)
    {
        auto opt_message
            = co_await awaitable_or_fin(fin, player.wait_chat_message(AwTuple));
        if (!opt_message)
            co_return;
        if (std::get<0>(*opt_message))
            co_return;

        uint8_t packet_id = 0x79,
                is_overlay = std::get<2>(*opt_message) ? 0x01 : 0x00;
        asio::co_spawn(transport.socket.get_executor(),
            awaitable_or_fin(fin,
                packetops::put_va(transport, asio::buffer(&packet_id, 1),
                    asio::buffer(*std::get<1>(*opt_message)),
                    asio::buffer(&is_overlay, 1))),
            [&fin](std::exception_ptr exc_ptr,
                std::optional<sys::error_code> opt_ec)
            {
                if (exc_ptr)
                    std::rethrow_exception(exc_ptr);
                if (opt_ec && *opt_ec)
                    fin.emit(*opt_ec, "System Chat Message");
            });
    }
}
