module;
#include <boost/asio.hpp>
#include <boost/system.hpp>
#include <memory>
#include <utility>
export module actualklasterkraft.net.play.session;

import actualklasterkraft.generic.pubsub;
import actualklasterkraft.world.player;
import actualklasterkraft.net.base.transport;
import actualklasterkraft.net.play.packetrouter;

namespace asio = boost::asio;
namespace sys = boost::system;

// Shared ownership for everything a play connection touches.
//
// Problem: play() used to keep Transport / streambuf / PacketRouter as
// coroutine-frame locals and spawn detached loops referencing them.
// When the client disconnected, play() returned and destroyed those
// locals while the detached loops (keepalive, chat, pull-posrot, tab list,
// pending async_read in PacketRouter) were still suspended -> use-after-free,
// which later surfaced as a crash on the next Server List Ping (which walks
// the player pool that the dangling loops had corrupted/reused).
//
// PlaySession lives on the heap in a shared_ptr. Every loop holds a copy,
// so the transport / buffer / router stay alive until the last loop exits.
// play() signals `done`, waits for `pending` tracked tasks to reach zero
// (via `all_done`), and only then kills the Player pool slot, so a slot
// can never be reused while an old connection still references it.
export struct PlaySession : public std::enable_shared_from_this<PlaySession>
{
    Transport transport;
    asio::streambuf streambuf;
    PacketRouter router;
    Player *player { nullptr };

    // Emitted once when the connection starts tearing down.
    // Loops must select on it in every blocking wait.
    // NOTE: Signal is one-shot (not sticky): a waiter registered AFTER
    // emit() would hang forever. shutdown() therefore sets `dead` first,
    // and every wait site checks `dead` before suspending (single io
    // thread => check-then-suspend is race-free).
    Signal<void(sys::error_code)> done;
    bool dead { false };

    // Number of tracked detached tasks. Guarded by the io_context thread
    // (the server runs a single io.run()), so plain int is enough.
    int pending { 0 };
    // Emitted each time pending drops to zero.
    Signal<void(sys::error_code)> all_done;

    explicit PlaySession(Transport &&t)
        : transport(std::move(t))
        , router(transport, streambuf)
    {
    }

    PlaySession(const PlaySession &) = delete;
    PlaySession(PlaySession &&) = delete;
    PlaySession &operator=(const PlaySession &) = delete;
    PlaySession &operator=(PlaySession &&) = delete;
};

// RAII guard for a tracked task. Construct on task start; destruction
// decrements the counter and wakes a joining play() if it was the last one.
export struct SessionTaskGuard
{
    std::shared_ptr<PlaySession> session;

    explicit SessionTaskGuard(std::shared_ptr<PlaySession> s)
        : session(std::move(s))
    {
        ++session->pending;
    }

    SessionTaskGuard(const SessionTaskGuard &) = delete;
    SessionTaskGuard &operator=(const SessionTaskGuard &) = delete;

    ~SessionTaskGuard() noexcept
    {
        if (--session->pending == 0)
            session->all_done.emit({ });
    }
};
