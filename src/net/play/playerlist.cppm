module;
#include <array>
#include <boost/asio.hpp>
#include <boost/asio/experimental/awaitable_operators.hpp>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <ranges>
#include <vector>
export module actualklasterkraft.net.play.playerlist;

import actualklasterkraft.generic.completiontokens;
import actualklasterkraft.generic.errc;
import actualklasterkraft.generic.templates;
import actualklasterkraft.data.protocolprimitives;
import actualklasterkraft.world.player;
import actualklasterkraft.net.base.disconnecthelpers;
import actualklasterkraft.net.base.packetops;
import actualklasterkraft.net.base.transport;
import actualklasterkraft.net.play.session;

namespace asio = boost::asio;
namespace sys = boost::system;
using namespace protocolprimitives;
using namespace asio::experimental::awaitable_operators;
using ISI = std::istreambuf_iterator<char>;

export asio::awaitable<void> tab_list_loop(std::shared_ptr<PlaySession> session)
{
    SessionTaskGuard guard { session };
    Transport &transport = session->transport;
    Player &self = *session->player;

    std::array<uint8_t, 64> buf;
    sys::error_code ec;
    for (;;)
    {
        if (session->dead)
            co_return;

        auto variant = co_await (get_global_player_pool().wait_player_spawn(
                                     asio::as_tuple(asio::use_awaitable))
            || get_global_player_pool().wait_player_about_to_die(
                asio::as_tuple(asio::use_awaitable))
            || session->done.wait(asio::as_tuple(asio::use_awaitable)));
        if (std::get_if<2>(&variant))
            co_return;

        auto it = buf.begin();
        if (auto *tuple = std::get_if<0>(&variant))
        {
            const auto &[ec, other] = *tuple;
            if (ec)
                co_return;

            *it++ = 0x46; // Player Info Update
            *it++ = 0x09; // actions mask, Add Player | Update Listed
            *it++ = 1; // players count
            it = std::ranges::copy(other->get_uuid(), it).out;
            it = write_string(it, other->get_name());
            *it++ = 0; // no properties
            *it++ = true; // is listed
        }
        else
        {
            const auto &[ec, other] = std::get<1>(variant);
            if (ec || other == &self)
                co_return;

            *it++ = 0x45; // Player Info Remove
            *it++ = 1; // players count
            it = std::ranges::copy(other->get_uuid(), it).out;
        }

        ec = co_await packetops::put(
            transport, asio::buffer(buf.data(), it - buf.begin()));
        if (is_normal_shutdown(ec))
            co_return;
        else if (ec)
            co_return co_await disconnect::play(transport,
                disconnect::fmt_reason(ec, "Player Info Update/Remove"));
    }
};

export asio::awaitable<bool> init_tab_list(std::shared_ptr<PlaySession> session)
{
    Transport &transport = session->transport;

    // send Player Info Update with all players including us
    std::array<uint8_t, 8> buf1 {
        0x46, // packet ID
        0x09, // actions mask, Add Player | Update Listed
    };

    std::vector<uint8_t> buf2;
    buf2.reserve(50 * get_global_player_pool().count_taken_slots());
    size_t players_count { };

    for (Player &other : get_global_player_pool().living_players())
    {
        std::ranges::copy(other.get_uuid(), std::back_inserter(buf2));
        write_string(std::back_inserter(buf2), other.get_name());
        buf2.push_back(0); // no properties
        buf2.push_back(true); // is listed
        ++players_count;
    }
    auto buf1_end = write_var<uint32_t>(buf1.begin() + 2, players_count);

    auto ec = co_await packetops::put_va(transport,
        asio::buffer(buf1.data(), buf1_end - buf1.begin()), asio::buffer(buf2));
    if (ec)
    {
        if (!is_normal_shutdown(ec))
            co_await disconnect::play(
                transport, disconnect::fmt_reason(ec, "Update Player Info"));
        co_return false;
    }

    ++session->pending;
    asio::co_spawn(transport.socket.get_executor(),
        [session]() -> asio::awaitable<void>
        {
            co_await tab_list_loop(session);
            if (--session->pending == 0)
                session->all_done.emit();
        },
        detached_log_exceptions_token);

    co_return true;
}
