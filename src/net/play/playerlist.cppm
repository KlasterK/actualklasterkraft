module;
#include <array>
#include <boost/asio.hpp>
#include <boost/asio/experimental/awaitable_operators.hpp>
#include <cstddef>
#include <cstdint>
#include <ranges>
#include <vector>
export module actualklasterkraft.net.play.playerlist;

import actualklasterkraft.generic.errc;
import actualklasterkraft.generic.templates;
import actualklasterkraft.data.protocolprimitives;
import actualklasterkraft.world.player;
import actualklasterkraft.net.base.packetops;
import actualklasterkraft.net.base.transport;
import actualklasterkraft.net.play.finalisation;

namespace asio = boost::asio;
namespace sys = boost::system;
using namespace protocolprimitives;
using namespace asio::experimental::awaitable_operators;
static constexpr auto AwTuple = asio::as_tuple(asio::use_awaitable);

export asio::awaitable<void> detached_tab_list_loop(
    Transport &transport, Player &self, FinalisationSignal &fin)
{
    std::array<uint8_t, 64> buf;
    sys::error_code ec;
    for (;;)
    {
        auto opt_variant = co_await awaitable_or_fin(fin,
            get_global_player_pool().wait_player_spawn(AwTuple)
                || get_global_player_pool().wait_player_about_to_die(AwTuple));
        if (!opt_variant)
            co_return;

        auto it = buf.begin();
        if (auto *tuple = std::get_if<0>(&*opt_variant))
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
            const auto &[ec, other] = std::get<1>(*opt_variant);
            if (ec || other == &self)
                co_return;

            *it++ = 0x45; // Player Info Remove
            *it++ = 1; // players count
            it = std::ranges::copy(other->get_uuid(), it).out;
        }

        ec = co_await packetops::put(
            transport, asio::buffer(buf.data(), it - buf.begin()));
        if (ec)
            co_return fin.emit(ec, "Player Info Update/Remove");
    }
};

export asio::awaitable<std::tuple<sys::error_code, std::string_view>>
init_tab_list(Transport &transport, Player &self)
{
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
        co_return std::tuple { ec, "Update Player Info" };

    co_return std::tuple { sys::error_code { }, "" };
}
