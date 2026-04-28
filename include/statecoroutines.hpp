#ifndef ACTKK_STATECOROUTINES_HPP
#define ACTKK_STATECOROUTINES_HPP

#include <boost/asio.hpp>
#include <boost/intrusive_ptr.hpp>
#include <boost/system.hpp>

class Session;

namespace statecoroutines
{
    namespace asio = boost::asio;
    namespace sys = boost::system;
    using boost::asio::ip::tcp;

    asio::awaitable<sys::error_code> handshake(
        boost::intrusive_ptr<Session> session);

    asio::awaitable<sys::error_code> status(
        boost::intrusive_ptr<Session> session);

    asio::awaitable<sys::error_code> login(
        boost::intrusive_ptr<Session> session, bool is_transfer);

    asio::awaitable<sys::error_code> configuration(
        boost::intrusive_ptr<Session> session, std::string &&player_name,
        std::array<uint8_t, 16> &&player_uuid);
}

#endif // ACTKK_STATECOROUTINES_HPP
