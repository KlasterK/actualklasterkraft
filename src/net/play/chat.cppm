module;
#include <array>
#include <boost/asio.hpp>
#include <boost/asio/experimental/awaitable_operators.hpp>
#include <boost/smart_ptr/make_local_shared.hpp>
#include <iterator>
#include <memory>
#include <new>
#include <print>
#include <string_view>
export module actualklasterkraft.net.play.chat;

import actualklasterkraft.generic.errc;
import actualklasterkraft.generic.pubsub;
import actualklasterkraft.generic.templates;
import actualklasterkraft.data.nbtbuilder;
import actualklasterkraft.data.protocolprimitives;
import actualklasterkraft.world.player;
import actualklasterkraft.net.base.disconnecthelpers;
import actualklasterkraft.net.base.packetops;
import actualklasterkraft.net.base.transport;
import actualklasterkraft.net.play.packetrouter;
import actualklasterkraft.net.play.session;

namespace asio = boost::asio;
namespace sys = boost::system;
using namespace protocolprimitives;
using namespace asio::experimental::awaitable_operators;
using ISI = std::istreambuf_iterator<char>;

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

export asio::awaitable<void> chat_message_loop(
    std::shared_ptr<PlaySession> session)
{
    SessionTaskGuard guard { session };
    Transport &transport = session->transport;
    asio::streambuf &sb = session->streambuf;
    PacketRouter &packet_router = session->router;
    Player &player = *session->player;

    Signal<void(sys::error_code)> signal;
    auto sub = packet_router.subscribe(
        0x09, [&](sys::error_code ec, uint32_t) { signal.emit(ec); });

    // See keepalive_loop: bail out if the router died before we subscribed.
    if (packet_router.dead())
        co_return;

    for (;;)
    {
        if (session->dead)
            co_return;

        auto variant = co_await (signal.wait(asio::as_tuple(asio::use_awaitable))
            || session->done.wait(asio::as_tuple(asio::use_awaitable)));
        if (std::get_if<1>(&variant))
            co_return;
        auto [ec] = std::move(std::get<0>(variant));
        if (is_normal_shutdown(ec))
            co_return;
        else if (ec)
            co_return co_await disconnect::play(
                transport, disconnect::fmt_reason(ec, "Chat Message"));

        auto message_len = InlineTie(TieReturn, std::ignore, std::ignore)
            = read_var<uint32_t>(ISI(&sb), ISI());
        if (message_len == 0) // it's 0 on errors too
            co_return co_await disconnect::play(
                transport, "You can't send an empty message. Don't abuse us.");
        if (message_len > 256)
            co_return co_await disconnect::play(transport,
                "You can't send a message longer than 256 characters. Don't abuse us.");

        std::array<char, 512> buf;
        auto it = buf.data();
        *it++ = '<';
        it = std::ranges::copy(player.get_name(), it).out;
        *it++ = '>';
        *it++ = ' ';
        if (message_len != sb.sgetn(it, message_len))
            co_return co_await disconnect::play(transport,
                disconnect::fmt_reason(
                    MCProtocolError::UnsufficientPacketData, "Chat Message"));

        // Ignore Mojang crypto shit part of the packet
        sb.consume(sb.size());

        send_usual_message_to_all(
            std::string_view(buf.data(), it + message_len));
    }
}

export asio::awaitable<void> send_system_chat_message(
    std::shared_ptr<PlaySession> session,
    Player::SharedTextComponent text_component, bool is_overlay)
{
    SessionTaskGuard guard { session };
    Transport &transport = session->transport;

    uint8_t packet_id = 0x79, u8_is_overlay = is_overlay ? 0x01 : 0x00;
    auto ec = co_await packetops::put_va(transport, asio::buffer(&packet_id, 1),
        asio::buffer(*text_component), asio::buffer(&u8_is_overlay, 1));
    if (is_normal_shutdown(ec))
        co_return;
    else if (ec)
        co_await disconnect::play(
            transport, disconnect::fmt_reason(ec, "System Chat Message"));
}
