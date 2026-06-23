
module;
#include <boost/asio.hpp>
#include <boost/intrusive_ptr.hpp>
#include <boost/smart_ptr/intrusive_ref_counter.hpp>
#include <boost/system.hpp>
#include <print>
export module actualklasterkraft.statecoroutines.status;

import actualklasterkraft.errc;
import actualklasterkraft.packetops;
import actualklasterkraft.session;
import actualklasterkraft.streambufops;

namespace asio = boost::asio;
namespace sys = boost::system;

export namespace statecoroutines
{
    asio::awaitable<sys::error_code> status(
        boost::intrusive_ptr<Session> session)
    {
        std::println("\tstate_status");

        // Getting packet Status Request
        auto packet_result = co_await packetops::await_for_packet(*session);
        if (!packet_result)
            co_return packet_result.error();

        // Packet ID
        if (session->get_streambuf().sbumpc() != 0x00)
            co_return MCProtocolError::UnexpectedPacketID;

        if (session->get_streambuf().in_avail() != 0)
            co_return MCProtocolError::ExcessPacketData;
        session->get_streambuf().consume(session->get_streambuf().size());

        static constexpr std::string_view ExampleResponse = R"({
            "version": {
                "name": "26.1.2",
                "protocol": 775
            },
            "players": {
                "max": 20,
                "online": 1,
                "sample": []
            },
            "description": {
                "text": "Hello, world!"
            },
            "enforcesSecureChat": false
        })";

        session->get_streambuf().sputc(0x00); // Status Response
        streambufops::write_vari32(
            session->get_streambuf(), ExampleResponse.size());
        session->get_streambuf().sputn(
            ExampleResponse.data(), ExampleResponse.size());

        auto ec = co_await packetops::flush_packet(*session);
        if (ec)
            co_return ec;

        // Getting packet Ping Request
        packet_result = co_await packetops::await_for_packet(*session);
        if (!packet_result)
            co_return packet_result.error();

        // Packet ID
        if (session->get_streambuf().sbumpc() != 0x01)
            co_return MCProtocolError::UnexpectedPacketID;

        auto payload_opt
            = streambufops::read_integer<uint64_t>(session->get_streambuf());
        if (!payload_opt)
            co_return MCProtocolError::UnsufficientPacketData;

        if (session->get_streambuf().in_avail() != 0)
            co_return MCProtocolError::ExcessPacketData;
        session->get_streambuf().consume(session->get_streambuf().size());

        // Pong Response
        session->get_streambuf().sputc(0x01); // ID
        streambufops::write_integer<uint64_t>(
            session->get_streambuf(), *payload_opt);

        ec = co_await packetops::flush_packet(*session);
        if (ec)
            co_return ec;

        session->get_socket().close();
        co_return boost::system::error_code { };
    }
}
