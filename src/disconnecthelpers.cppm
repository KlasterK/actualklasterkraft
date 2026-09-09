module;
#include <boost/asio.hpp>
#include <boost/container/small_vector.hpp>
#include <boost/system.hpp>
#include <print>
#include <string_view>
export module actualklasterkraft.disconnecthelpers;

import actualklasterkraft.errc;
import actualklasterkraft.formatters;
import actualklasterkraft.nbtbuilder;
import actualklasterkraft.packetops;
import actualklasterkraft.protocolprimitives;
import actualklasterkraft.templates;
import actualklasterkraft.transport;

using namespace protocolprimitives;
using namespace std::literals;
namespace asio = boost::asio;
namespace sys = boost::system;
using asio::ip::tcp;
using PacketVec = boost::container::small_vector<uint8_t, 256>;

boost::asio::awaitable<void> epilog(
    Transport &transport, PacketVec vec, std::string reason)
{
    auto ec = co_await packetops::put(transport, asio::buffer(vec));
    if (ec)
        std::println(
            "Client {} was disconnected but reason could not be sent (error code: {}; reason: {})",
            transport.remote_endpoint_copy, ec, reason);
    else
        std::println("Client {} was disconnected with reason: {}",
            transport.remote_endpoint_copy, reason);

    transport.socket.shutdown(tcp::socket::shutdown_both);
    transport.socket.close();
}

export namespace disconnect
{
    auto login(Transport &transport, std::string reason)
    {
        constexpr auto json_left = "{\"text\":\""sv, json_right = "\"}"sv;

        PacketVec vec;
        vec.push_back(0x00); // packet ID
        write_var<uint32_t>(std::back_inserter(vec),
            json_left.size() + reason.size() + json_right.size());

        std::ranges::copy(json_left, std::back_inserter(vec));
        for (char c : reason)
        {
            if (c == '"' || c == '\\')
                vec.push_back('\\');
            vec.push_back(c);
        }
        std::ranges::copy(json_right, std::back_inserter(vec));

        return epilog(transport, std::move(vec), std::move(reason));
    }

    auto configuration(Transport &transport, std::string reason)
    {
        PacketVec vec;
        vec.push_back(0x02); // packet ID
        NBTBuilder(std::back_inserter(vec)) << nbttags::String << reason;
        return epilog(transport, std::move(vec), std::move(reason));
    }

    auto play(Transport &transport, std::string reason)
    {
        PacketVec vec;
        vec.push_back(0x20); // packet ID
        NBTBuilder(std::back_inserter(vec)) << nbttags::String << reason;
        return epilog(transport, std::move(vec), std::move(reason));
    }

    std::string fmt_desync(sys::error_code ec, std::string_view opt_ctx)
    {
        return opt_ctx.empty()
            ? std::format("Protocol Desync : {}", ec)
            : std::format("Protocol Desync ({}) : {}", opt_ctx, ec);
    }
}
