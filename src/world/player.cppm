module;
#include <bit>
#include <boost/asio.hpp>
#include <boost/asio/io_context.hpp>
#include <utility>
export module actualklasterkraft.world.player;

import actualklasterkraft.bitfields;
import actualklasterkraft.pubsub;
import actualklasterkraft.world.math;

namespace asio = boost::asio;
namespace sys = boost::system;

export class Player
{
public:
    struct PosRot
    {
        Vec3<double> position;
        Angle pitch;
        Angle yaw;
        uint8_t is_on_ground : 1;
        uint8_t is_pushing_against_wall : 1;
        uint8_t is_position_present : 1;
        uint8_t is_rotation_present : 1;
    };

public:
    Player(asio::io_context &io)
        : m_posrot_signal(io)
    {
    }

    const PosRot &get_last_posrot() const { return m_last_posrot; }

    void update_posrot(PosRot posrot)
    {
        m_last_posrot = posrot;
        m_posrot_signal.emit({ }, posrot);
    }

    auto await_for_posrot_update(auto &&completion_token)
    {
        return m_posrot_signal.wait(std::move(completion_token));
    }

private:
    WeakSignal<void(sys::error_code, PosRot)> m_posrot_signal;
    PosRot m_last_posrot;

    void spawn(PosRot posrot) { m_last_posrot = posrot; }

    void kill() { m_last_posrot = { { }, 0, 0, 0, 0, 0, 0 } };

    friend class PlayerPool;
};

// template <size_t N>
constexpr size_t N = 128;
class PlayerPool
{
    static_assert(N % 64 == 0);

public:
    PlayerPool(asio::io_context &io)
        : m_players(
              [&]<size_t... Is>(std::index_sequence<Is...>)
              {
                  return new std::array<Player, N> { (
                      (void)Is, Player(io))... };
              }(std::make_index_sequence<N>()))
        , m_bitmaps(new std::array<uint64_t, N / 64> { })
    {
    }

    Player *spawn()
    {
        for (uint64_t &bitmap : *m_bitmaps)
        {
            int zero_pos = std::countr_one(bitmap);
            if (zero_pos >= 64)
                continue;

            m_bitmaps.reset();
        }
        return nullptr;
    }

    void kill(Player *player)
    {
        assert(m_players.begin());
        ;
    }

private:
    std::unique_ptr<std::array<Player, N>> m_players;
    std::unique_ptr<std::array<uint64_t, N / 64>> m_bitmaps;
};

struct S
{
    virtual ~S() = default;
};

struct F : S
{
};

void foo() { (void)dynamic_cast<void *>((S *)nullptr); }
