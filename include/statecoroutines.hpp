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
}

#endif // ACTKK_STATECOROUTINES_HPP
