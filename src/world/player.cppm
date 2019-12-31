module;
#include <bit>
#include <boost/asio.hpp>
#include <boost/asio/io_context.hpp>
#include <cstddef>
#include <iterator>
#include <ranges>
#include <type_traits>
#include <utility>
export module actualklasterkraft.world.player;

import actualklasterkraft.bitfields;
import actualklasterkraft.errc;
import actualklasterkraft.pubsub;
import actualklasterkraft.world.math;

namespace asio = boost::asio;
namespace sys = boost::system;

export class Player
{
public:
    enum class State
    {
        Alive,
        Dead,
    };

    struct PosRot
    {
        Vec3<double> position;
        Angle pitch;
        Angle yaw;
        uint8_t is_on_ground : 1 = 0;
        uint8_t is_pushing_against_wall : 1 = 0;
        uint8_t is_position_present : 1 = 0;
        uint8_t is_rotation_present : 1 = 0;

        void partial_update(const PosRot &other)
        {
            if (other.is_position_present)
            {
                position = other.position;
                is_position_present = 1;
            }

            if (other.is_rotation_present)
            {
                pitch = other.pitch;
                yaw = other.yaw;
                is_rotation_present = 1;
            }

            is_on_ground = other.is_on_ground;
            is_pushing_against_wall = other.is_pushing_against_wall;
        }
    };

public:
    State get_state() const { return m_state; }

    const PosRot &get_last_posrot() const { return m_last_posrot; }

    void update_posrot(PosRot posrot)
    {
        m_last_posrot.partial_update(posrot);
        m_posrot_signal.emit({ }, m_last_posrot);
    }

    auto await_for_posrot_update(auto &&completion_token)
    {
        return m_posrot_signal.wait(std::move(completion_token));
    }

private:
    template <size_t N> friend class PlayerPool;

    Player(asio::any_io_executor io)
        : m_posrot_signal(io)
    {
    }

    void spawn(PosRot posrot)
    {
        m_state = State::Alive;
        m_last_posrot.partial_update(posrot);
    }

    void kill()
    {
        m_state = State::Dead;
        m_last_posrot = { };
        m_posrot_signal.emit(MCProtocolError::EntityWasKilled, { });
    }

private:
    WeakSignal<void(sys::error_code, PosRot)> m_posrot_signal;
    PosRot m_last_posrot;
    State m_state { State::Dead };
};

template <size_t N> class PlayerPool
{
public:
    static_assert(N > 0 && N % 64 == 0);

    class LivingPlayersIterator
    {
    public:
        using iterator_concept = std::forward_iterator_tag;
        using iterator_category = std::forward_iterator_tag;
        using value_type = Player;
        using difference_type = std::ptrdiff_t;
        using reference = Player &;

    public:
        LivingPlayersIterator() noexcept = default;

        bool operator==(const LivingPlayersIterator &other) const noexcept
        {
            return m_pool == other.m_pool && m_idx == other.m_idx;
        }

        Player &operator*() const noexcept
        {
            assert(m_idx < N);
            Player &player = (*m_pool->m_players)[m_idx];
            assert(player.get_state() == Player::State::Alive
                && "Semantic contract violation");
            return player;
        }

        Player *operator->() const noexcept { return &**this; }

        LivingPlayersIterator &operator++() noexcept
        {
            size_t bitmap_idx = m_idx / 64;
            int bit_idx = m_idx % 64 + 1;

            if (bit_idx == 64)
            {
                ++bitmap_idx;
                bit_idx = 0;
            }

            for (; bitmap_idx < N / 64; ++bitmap_idx)
            {
                uint64_t bitmap = (*m_pool->m_bitmaps)[bitmap_idx];
                bitmap >>= bit_idx;

                int one_shifted_pos = std::countr_zero(bitmap);
                if (one_shifted_pos != 64)
                {
                    m_idx = bitmap_idx * 64 + one_shifted_pos + bit_idx;
                    return *this;
                }
                bit_idx = 0;
            }

            m_idx = N;
            return *this;
        }

        LivingPlayersIterator operator++(int) noexcept
        {
            auto tmp = *this;
            ++*this;
            return tmp;
        }

    private:
        friend PlayerPool;

        LivingPlayersIterator(PlayerPool &pool, size_t idx)
            : m_pool(&pool)
            , m_idx(idx)
        {
        }

    private:
        PlayerPool *m_pool = nullptr;
        size_t m_idx = 0;
    };
    static_assert(std::forward_iterator<LivingPlayersIterator>);

public:
    PlayerPool(asio::any_io_executor io)
        : m_players(
              [&]<size_t... Is>(std::index_sequence<Is...>)
              {
                  return new std::array<Player, N> { (
                      (void)Is, Player(io))... };
              }(std::make_index_sequence<N>()))
        , m_bitmaps(new std::array<uint64_t, N / 64> { })
        , m_player_spawn_signal(io)
        , m_player_about_to_die_signal(io)
    {
    }

    Player *spawn(Player::PosRot posrot)
    {
        for (size_t i { }; i < N / 64; ++i)
        {
            auto &bitmap = (*m_bitmaps)[i];

            int zero_pos = std::countr_one(bitmap);
            if (zero_pos >= 64)
                continue;

            bitmap |= (uint64_t(1) << zero_pos);

            auto &player = (*m_players)[i * 64 + zero_pos];
            player.spawn(posrot);
            m_player_spawn_signal.emit({ }, player);
            return &player;
        }
        return nullptr;
    }

    void kill(Player &player)
    {
        size_t idx = &player - m_players->data();
        assert(idx < N);

        auto &bitmap = (*m_bitmaps)[idx / 64];
        auto bitmask = ~(uint64_t(1) << (idx % 64));
        bitmap &= bitmask;

        player.kill();
    }

    auto await_for_player_spawn(auto &&completion_token)
    {
        return m_player_spawn_signal.wait(completion_token);
    }

    auto await_for_player_about_to_die(auto &&completion_token)
    {
        return m_player_about_to_die_signal.wait(completion_token);
    }

    auto living_players()
    {
        LivingPlayersIterator begin { *this, 0 }, end { *this, N };
        if (0 == (m_bitmaps->front() & 1))
            ++begin;
        return std::ranges::subrange(begin, end);
    }

private:
    std::unique_ptr<std::array<Player, N>> m_players;
    std::unique_ptr<std::array<uint64_t, N / 64>> m_bitmaps;
    WeakSignal<void(sys::error_code, Player &)> m_player_spawn_signal,
        m_player_about_to_die_signal;
};

std::optional<PlayerPool<256>> g_player_pool;

export void emplace_global_player_pool(asio::any_io_executor io)
{
    g_player_pool.emplace(io);
}

export PlayerPool<256> &get_global_player_pool()
{
    assert(g_player_pool
        && "get_global_player_pool() called before emplace_global_player_pool()");
    return *g_player_pool;
}
