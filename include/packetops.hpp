#ifndef ACTKK_PACKETOPS_HPP
#define ACTKK_PACKETOPS_HPP

#include <boost/asio.hpp>
#include <boost/system.hpp>
#include <cstddef>
#include <expected>

class Session;

namespace packetops
{
    /// Awaits for a packet and reads it into the streambuf
    /// (without Packet Length).
    ///
    /// @returns Packet size or error code.
    boost::asio::awaitable<std::expected<size_t, boost::system::error_code>>
    await_for_packet(Session &session);

    boost::asio::awaitable<boost::system::error_code> flush_packet(
        Session &session);
}

#endif // ACTKK_PACKETOPS_HPP
