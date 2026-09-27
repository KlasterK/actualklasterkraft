module;
#include <boost/asio.hpp>
#include <boost/system.hpp>
#include <print>
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

export const auto detached_rethrow_token = [](std::exception_ptr exc_ptr)
{
    if (!exc_ptr)
        return;

    try
    {
        std::rethrow_exception(exc_ptr);
    }
    catch (const std::exception &exc)
    {
        std::println(
            "Unhandled exception in a detached async operation!\nWhat: {}",
            exc.what());
    }
};
