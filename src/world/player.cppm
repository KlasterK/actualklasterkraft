module;
#include <bit>
#include <boost/asio.hpp>
#include <boost/container/small_vector.hpp>
#include <cstddef>
#include <iterator>
#include <numeric>
#include <optional>
#include <ranges>
#include <utility>
export module actualklasterkraft.world.player;

import actualklasterkraft.bitfields;
import actualklasterkraft.errc;
import actualklasterkraft.pubsub;
import actualklasterkraft.world.math;

namespace asio = boost::asio;
namespace sys = boost::system;

/******************************************************************************/

export struct PosRot
{
    Vec3<double> position;
    Angle pitch;
    Angle yaw;
    bool is_on_ground : 1 = false;
    bool is_pushing_against_wall : 1 = false;
    bool is_position_present : 1 = false;
    bool is_rotation_present : 1 = false;

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

/******************************************************************************/

export class Player
{
public:
    using TextComponentStorage = boost::container::small_vector<uint8_t, 64>;
    using SharedTextComponent = std::shared_ptr<TextComponentStorage>;

    enum class State
    {
        Alive,
        Dying,
        Dead,
    };

    struct SpawnInfo
    {
        std::string name;
        std::array<uint8_t, 16> uuid;
    };

public:
    Player(const Player &) = delete;
    Player(Player &&) = delete;
    Player &operator=(const Player &) = delete;
    Player &operator=(Player &&) = delete;

    uint32_t get_eid() const { return m_eid; }
    State get_state() const { return m_state; }
    const PosRot &get_posrot() const { return m_posrot; }
    std::string_view get_name() const { return m_spawn_info.name; }
    std::span<const uint8_t, 16> get_uuid() const { return m_spawn_info.uuid; }

    void update_posrot(PosRot posrot)
    {
        m_posrot.partial_update(posrot);
        m_on_posrot_update.emit({ }, std::move(posrot));
    }

    void notify_player_entered_simulation_distance(Player &other)
    {
        m_on_player_enter_simulation_distance.emit({ }, &other);
    }

    void notify_player_exited_simulation_distance(Player &other)
    {
        m_on_player_exit_simulation_distance.emit({ }, &other);
    }

    void send_chat_message(
        SharedTextComponent text_component, bool is_overlay = false)
    {
        m_on_chat_message.emit({ }, std::move(text_component), is_overlay);
    }

#define KK_GENERATE_WAIT_METHOD(name)                                          \
    template <typename T> auto wait_##name(T &&tok)                            \
    {                                                                          \
        return m_on_##name.wait(std::forward<T>(tok));                         \
    }
    KK_GENERATE_WAIT_METHOD(posrot_update)
    KK_GENERATE_WAIT_METHOD(about_to_die)
    KK_GENERATE_WAIT_METHOD(player_enter_simulation_distance)
    KK_GENERATE_WAIT_METHOD(player_exit_simulation_distance)
    KK_GENERATE_WAIT_METHOD(chat_message)
#undef KK_GENERATE_WAIT_METHOD

private:
    template <size_t N> friend class PlayerPool;

    Player(uint32_t eid, asio::any_io_executor io)
        : m_eid(eid)
        , m_on_posrot_update(io)
        , m_on_about_to_die(io)
        , m_on_player_enter_simulation_distance(io)
        , m_on_player_exit_simulation_distance(io)
        , m_on_chat_message(io)
    {
    }

    void spawn(PosRot posrot, SpawnInfo &&spawn_info)
    {
        m_state = State::Alive;
        m_posrot.partial_update(posrot);
        m_posrot.is_position_present = true;
        m_posrot.is_rotation_present = true;
        m_spawn_info = std::move(spawn_info);
    }

    void begin_death()
    {
        m_state = State::Dying;
        m_on_about_to_die.emit({ });
    }

    void end_death()
    {
        m_state = State::Dead;
        m_posrot = { };
        m_spawn_info = { };

        m_on_posrot_update.emit(MCProtocolError::EntityWasKilled, { });
        // In case somebody started waiting OnAboutToDie in an OnAboutToDie handler
        m_on_about_to_die.emit(MCProtocolError::EntityWasKilled);
        m_on_player_enter_simulation_distance.emit(
            MCProtocolError::EntityWasKilled, nullptr);
        m_on_player_exit_simulation_distance.emit(
            MCProtocolError::EntityWasKilled, nullptr);
        m_on_chat_message.emit(
            MCProtocolError::EntityWasKilled, nullptr, false);
    }

private:
    uint32_t m_eid;
    State m_state { State::Dead };
    PosRot m_posrot;
    SpawnInfo m_spawn_info;

    Signal<void(sys::error_code, PosRot)> m_on_posrot_update;
    Signal<void(sys::error_code)> m_on_about_to_die;
    Signal<void(sys::error_code, Player *)>
        m_on_player_enter_simulation_distance,
        m_on_player_exit_simulation_distance;
    Signal<void(sys::error_code, SharedTextComponent, bool)> m_on_chat_message;
};

/******************************************************************************/

export template <typename> class LivingPlayersIterator;

export template <template <size_t> typename PoolT, size_t N>
class LivingPlayersIterator<PoolT<N>>
{
public:
    using iterator_concept = std::forward_iterator_tag;
    using iterator_category = std::forward_iterator_tag;
    using value_type = Player;
    using difference_type = std::ptrdiff_t;
    using reference = Player &;
    using pointer = Player *;

public:
    LivingPlayersIterator() noexcept = default;

    bool operator==(const LivingPlayersIterator &other) const noexcept
    {
        return m_pool == other.m_pool && m_idx == other.m_idx;
    }

    reference operator*() const noexcept
    {
        assert(m_idx < N);
        return (*m_pool->m_players)[m_idx];
    }

    pointer operator->() const noexcept { return &**this; }

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
            uint64_t bitmap = m_pool->m_bitmaps[bitmap_idx];
            bitmap &= ~uint64_t(0) << bit_idx; // clear bits lower than bit_idx

            while (bitmap != 0)
            {
                int one_pos = std::countr_zero(bitmap);

                m_idx = bitmap_idx * 64 + one_pos;
                if (this[0]->get_state() == Player::State::Alive)
                    return *this;

                bitmap &= bitmap - 1; // clear last set bit
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
    friend PoolT<N>;

    LivingPlayersIterator(PoolT<N> &pool, size_t idx)
        : m_pool(&pool)
        , m_idx(idx)
    {
    }

private:
    PoolT<N> *m_pool = nullptr;
    size_t m_idx = 0;
};

/******************************************************************************/

export template <size_t N> class PlayerPool
{
public:
    static_assert(N > 0 && N % 64 == 0);
    static constexpr uint32_t EIDBase = 0;

public:
    PlayerPool(asio::any_io_executor io)
        : m_players(
              [&]<size_t... Is>(std::index_sequence<Is...>)
              {
                  return new std::array<Player, N> { Player(
                      EIDBase + Is, io)... };
              }(std::make_index_sequence<N>()))
        , m_on_player_spawn(io)
        , m_on_player_about_to_die(io)
    {
    }

    Player *spawn(PosRot posrot, Player::SpawnInfo &&spawn_info)
    {
        for (size_t i { }; i < N / 64; ++i)
        {
            auto &bitmap = m_bitmaps[i];

            int zero_pos = std::countr_one(bitmap);
            if (zero_pos >= 64)
                continue;

            bitmap |= (uint64_t(1) << zero_pos);

            auto &player = (*m_players)[i * 64 + zero_pos];
            player.spawn(posrot, std::move(spawn_info));
            m_on_player_spawn.emit({ }, &player);
            return &player;
        }
        return nullptr;
    }

    void kill(Player &player)
    {
        if (player.get_state() != Player::State::Alive)
            return;

        m_on_player_about_to_die.wait(
            [this](sys::error_code ec, Player *player)
            {
                if (!player || ec)
                    return;

                size_t idx = player - m_players->data();
                assert(idx < N);
                m_bitmaps[idx / 64] &= ~(uint64_t(1) << (idx % 64));

                player->end_death();
            });

        player.begin_death();
        m_on_player_about_to_die.emit({ }, &player);
    }

    auto wait_player_spawn(auto &&tok)
    {
        return m_on_player_spawn.wait(std::forward<decltype(tok)>(tok));
    }

    auto wait_player_about_to_die(auto &&tok)
    {
        return m_on_player_about_to_die.wait(std::forward<decltype(tok)>(tok));
    }

    auto living_players()
    {
        LivingPlayersIterator<PlayerPool> begin { *this, 0 }, end { *this, N };
        if (m_players->front().get_state() != Player::State::Alive)
            ++begin;
        return std::ranges::subrange(begin, end);
    }

    size_t count_taken_slots()
    {
        return std::accumulate(m_bitmaps.begin(), m_bitmaps.end(), 0zu,
            [](size_t sum, uint64_t bitmap)
            { return sum + std::popcount(bitmap); });
    }

    constexpr size_t max_players() { return N; }

private:
    friend LivingPlayersIterator<PlayerPool>;
    static_assert(std::forward_iterator<LivingPlayersIterator<PlayerPool>>);

    std::unique_ptr<std::array<Player, N>> m_players;
    Signal<void(sys::error_code, Player *)> m_on_player_spawn,
        m_on_player_about_to_die;

    std::array<uint64_t, N / 64> m_bitmaps { };
};

/******************************************************************************/

std::optional<PlayerPool<256>> g_player_pool;

export void emplace_global_player_pool(asio::any_io_executor io)
{
    g_player_pool.emplace(io);
}

export auto &get_global_player_pool()
{
    assert(g_player_pool
        && "get_global_player_pool() called before emplace_global_player_pool()");
    return *g_player_pool;
}
