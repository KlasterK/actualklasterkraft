#include "packetops.hpp"
#include "session.hpp"
#include "statecoroutines.hpp"
#include "streambufops.hpp"
#include <boost/asio/co_spawn.hpp>
#include <boost/system/detail/error_code.hpp>
#include <print>

using namespace statecoroutines;

asio::awaitable<sys::error_code> statecoroutines::handshake(
    boost::intrusive_ptr<Session> session)
{
    std::println("\tstate_handshake");

    auto close = [&]
    {
        session->get_socket().close();
        return sys::error_code { };
    };

    auto packet_result = co_await packetops::await_for_packet(*session);
    if (!packet_result)
        co_return close();

    // Packet ID
    if (session->get_streambuf().sbumpc() != 0x00)
        co_return close();

    // Protocol Version
    auto vari32_result = streambufops::read_vari32(session->get_streambuf());
    if (!vari32_result)
        co_return close();
    // Don't check version for now
    std::println("\tProtocol Version: {}", *vari32_result);

    // Server Address
    vari32_result = streambufops::read_vari32(session->get_streambuf());
    if (!vari32_result)
        co_return close();

    // Don't use this string, skip bytes. But can't move gptr in streambuf
    // without reading, use a temp buffer to do it.
    {
        std::string dummy(*vari32_result, '\0');
        session->get_streambuf().sgetn(dummy.data(), *vari32_result);
    }

    // Server Port, u16, won't use
    if (!streambufops::read_integer<uint16_t>(session->get_streambuf())
            .has_value())
        co_return close();

    // Next State
    vari32_result = streambufops::read_vari32(session->get_streambuf());
    if (!vari32_result || *vari32_result != 1) // Status
        co_return close();

    if (session->get_streambuf().in_avail() != 0)
        co_return close();

    session->get_streambuf().consume(session->get_streambuf().size());

    asio::co_spawn(session->get_io(), statecoroutines::status(session),
        [session](std::exception_ptr exc_ptr, sys::error_code ec)
        { session->handle_coroutine_finished(exc_ptr, ec); });

    co_return boost::system::error_code { };
}
