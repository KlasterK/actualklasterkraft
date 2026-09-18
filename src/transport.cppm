module;
#include <boost/asio.hpp>
#include <boost/system.hpp>
#include <utility>
export module actualklasterkraft.transport;

namespace sys = boost::system;
using boost::asio::ip::tcp;

export struct Transport
{
    tcp::socket socket;
    tcp::endpoint local_endpoint_copy, remote_endpoint_copy;

    Transport(tcp::socket a_socket, sys::error_code &out_ec)
        : socket(std::move(a_socket))
        , local_endpoint_copy(socket.local_endpoint(out_ec))
        , remote_endpoint_copy(
              out_ec ? tcp::endpoint { } : socket.remote_endpoint(out_ec))
    {
    }
};
