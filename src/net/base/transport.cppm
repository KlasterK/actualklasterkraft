module;
#include <boost/asio.hpp>
#include <boost/system.hpp>
#include <utility>
export module actualklasterkraft.net.base.transport;

import actualklasterkraft.generic.pubsub;

namespace asio = boost::asio;
namespace sys = boost::system;
using boost::asio::ip::tcp;

export struct Transport
{
    tcp::socket socket;
    tcp::endpoint local_endpoint_copy, remote_endpoint_copy;
    Signal<void()> done_signal;

    Transport(tcp::socket a_socket, sys::error_code &out_ec)
        : socket(std::move(a_socket))
        , local_endpoint_copy(socket.local_endpoint(out_ec))
        , remote_endpoint_copy(
              out_ec ? tcp::endpoint { } : socket.remote_endpoint(out_ec))
    {
    }

    Transport(const Transport &) = delete;
    Transport &operator=(const Transport &) = delete;

    // NOTE: the user-declared destructor below suppresses the implicit
    // move constructor; without this explicit one every
    // std::move(transport) between stages fails to compile (it falls back
    // to the deleted copy). Moves only happen when no async ops are
    // pending on the socket, so this is safe.
    Transport(Transport &&) noexcept = default;
    Transport &operator=(Transport &&) noexcept = default;

    ~Transport()
    {
        done_signal.emit();
    }
};

export bool is_normal_shutdown(sys::error_code non_empty_ec)
{
    return non_empty_ec == asio::error::eof
        || non_empty_ec == asio::error::operation_aborted
        || non_empty_ec == asio::error::bad_descriptor;
}
