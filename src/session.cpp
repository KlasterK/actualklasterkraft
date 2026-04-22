#include "session.hpp"
#include "errc.hpp"
#include "streambufops.hpp"
#include <boost/intrusive_ptr.hpp>
#include <print>

namespace asio = boost::asio;
namespace sys = boost::system;
using tcp = boost::asio::ip::tcp;

void Session::print_streambuf()
{
    for (;;)
    {
        int byte = m_streambuf.sbumpc();
        if (byte < 0)
            break;

        std::print("{:02x} ", byte);
    }
    m_streambuf.consume(m_streambuf.size());
    std::println();
}

boost::asio::awaitable<std::expected<size_t, sys::error_code>>
Session::await_for_packet()
{
    char byte { };
    int32_t packet_size { };
    unsigned position { };
    sys::error_code ec { };

    for (;;)
    {
        co_await asio::async_read(m_sock, asio::buffer(&byte, 1),
            asio::transfer_exactly(1), asio::redirect_error(ec));
        if (ec)
            co_return std::unexpected(ec);

        packet_size |= (byte & 0b01111111) << position;
        if ((byte & 0b10000000) == 0)
            break;

        position += 7;
        if (position > 32)
            co_return std::unexpected(MCProtocolError::VarIntTooBig);
    }

    co_await asio::async_read(m_sock, m_streambuf,
        asio::transfer_exactly(packet_size), asio::redirect_error(ec));

    if (ec)
        co_return std::unexpected(ec);

    co_return packet_size;
}

boost::asio::awaitable<boost::system::error_code> Session::flush_packet()
{
    std::array<uint8_t, 5> size_buf;
    auto size_end = size_buf.begin();

    for (uint32_t value = m_streambuf.size();;)
    {
        if ((value & ~0x7F) == 0)
        {
            *size_end++ = value & 0xFF;
            break;
        }
        *size_end++ = (value & 0x7F) | 0x80;
        value >>= 7u;
    }

    auto bufs = std::to_array<asio::const_buffer>(
        { { size_buf.begin(),
              static_cast<size_t>(size_end - size_buf.begin()) },
            m_streambuf.data() });

    auto [ec, _] = co_await asio::async_write(m_sock, bufs, asio::as_tuple);
    m_streambuf.consume(m_streambuf.size());

    co_return ec;
}

boost::asio::awaitable<boost::system::error_code> Session::state_handshake()
{
    std::println("\tstate_handshake");

    auto packet_result = co_await await_for_packet();
    if (!packet_result)
    {
        m_sock.close();
        co_return boost::system::error_code { };
    };

    // Packet ID
    if (m_streambuf.sbumpc() != 0x00)
    {
        m_sock.close();
        co_return boost::system::error_code { };
    };

    // Protocol Version
    auto vari32_result = streambufops::read_vari32(m_streambuf);
    if (!vari32_result)
    {
        m_sock.close();
        co_return boost::system::error_code { };
    };
    // Don't check version for now
    std::println("\tProtocol Version: {}", *vari32_result);

    // Server Address
    vari32_result = streambufops::read_vari32(m_streambuf);
    if (!vari32_result)
    {
        m_sock.close();
        co_return boost::system::error_code { };
    };

    {
        // Don't use this string, skip bytes. But can't move from streambuf
        // without reading, use a temp buffer for it.
        std::string dummy(*vari32_result, '\0');
        m_streambuf.sgetn(dummy.data(), *vari32_result);
    }

    // Server Port, u16, won't use
    if (!streambufops::read_integer<uint16_t>(m_streambuf).has_value())
    {
        m_sock.close();
        co_return boost::system::error_code { };
    };

    // Next State
    vari32_result = streambufops::read_vari32(m_streambuf);
    if (!vari32_result || *vari32_result != 1) // Status
    {
        m_sock.close();
        co_return boost::system::error_code { };
    };

    m_state_cb = &Session::state_status;

    if (m_streambuf.in_avail() != 0)
    {
        m_sock.close();
        co_return boost::system::error_code { };
    };

    m_streambuf.consume(m_streambuf.size());
    co_return boost::system::error_code { };
}

boost::asio::awaitable<boost::system::error_code> Session::state_status()
{
    std::println("\tstate_status");

    // Getting packet Status Request
    auto packet_result = co_await await_for_packet();
    if (!packet_result)
        co_return packet_result.error();

    // Packet ID
    if (m_streambuf.sbumpc() != 0x00)
        co_return MCProtocolError::UnexpectedPacketID;

    if (m_streambuf.in_avail() != 0)
        co_return MCProtocolError::ExcessPacketData;
    m_streambuf.consume(m_streambuf.size());

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

    m_streambuf.sputc(0x00); // Status Response
    streambufops::write_vari32(m_streambuf, ExampleResponse.size());
    m_streambuf.sputn(ExampleResponse.data(), ExampleResponse.size());

    auto ec = co_await flush_packet();
    if (ec)
        co_return ec;

    // Getting packet Ping Request
    packet_result = co_await await_for_packet();
    if (!packet_result)
        co_return packet_result.error();

    // Packet ID
    if (m_streambuf.sbumpc() != 0x01)
        co_return MCProtocolError::UnexpectedPacketID;

    auto payload_opt = streambufops::read_integer<uint64_t>(m_streambuf);
    if (!payload_opt)
        co_return MCProtocolError::UnsufficientPacketData;

    if (m_streambuf.in_avail() != 0)
        co_return MCProtocolError::ExcessPacketData;
    m_streambuf.consume(m_streambuf.size());

    // Pong Response
    m_streambuf.sputc(0x01); // ID
    streambufops::write_integer<uint64_t>(m_streambuf, *payload_opt);

    ec = co_await flush_packet();
    if (ec)
        co_return ec;

    m_sock.close();
    co_return boost::system::error_code { };
}

Session::Session(
    boost::asio::io_context &io, boost::asio::ip::tcp::socket &&sock)
    : m_io(io)
    , m_sock(std::move(sock))
{
    std::ostringstream oss;
    oss << m_sock.remote_endpoint();
    m_remote_endpoint_name = std::move(oss.str());
}

void Session::next_packet()
{
    boost::asio::co_spawn(m_io, std::invoke(m_state_cb, this),
        [self = boost::intrusive_ptr(this)](
            std::exception_ptr e_ptr, boost::system::error_code ec)
        {
            if (e_ptr)
                std::rethrow_exception(e_ptr);

            if (ec)
            {
                std::println("Connection from {} caused an error: {}",
                    self->m_remote_endpoint_name, ec.what());
                self->m_sock.close();
                return;
            }

            if (!self->m_sock.is_open())
            {
                std::println("Connection {} was successfully closed",
                    self->m_remote_endpoint_name);
                return;
            }

            self->next_packet();
        });
}
