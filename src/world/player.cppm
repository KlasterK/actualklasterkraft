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

import actualklasterkraft.basepool;
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
    Angle head_yaw;
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
            head_yaw = other.head_yaw;
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

    Player() = default;

    void spawn(uint32_t eid, PosRot posrot, SpawnInfo &&spawn_info)
    {
        m_eid = eid;
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
    uint32_t m_eid { };
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

export template <size_t N> class PlayerPool : private BasePool<N>
{
private:
    using ValueType = Player;
    static constexpr uint32_t EIDBase = 0;

    friend PoolTakenSlotsIterator<PlayerPool>;
    Player &pool_iterator_dereference(size_t idx) { return m_players[idx]; }
    bool pool_iterator_test(size_t idx)
    {
        return m_players[idx].get_state() == Player::State::Alive;
    }

public:
    PlayerPool()
        : m_players(new Player[N])
    {
    }

    Player *spawn(PosRot posrot, Player::SpawnInfo &&spawn_info)
    {
        size_t idx = BasePool<N>::allocate();
        if (idx == N)
            return nullptr;

        auto &player = m_players[idx];
        player.spawn(EIDBase + idx, std::move(posrot), std::move(spawn_info));
        m_on_player_spawn.emit({ }, &player);
        return &player;
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

                BasePool<N>::free(player - m_players.get());
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
        PoolTakenSlotsIterator<PlayerPool> begin { *this, 0 }, end { *this, N };
        if (m_players[0].get_state() != Player::State::Alive)
            ++begin;
        return std::ranges::subrange(begin, end);
    }

    using BasePool<N>::count_taken_slots;

    constexpr size_t max_players() { return N; }

private:
    // It cannot be std::array because Player's dtor is private
    std::unique_ptr<Player[]> m_players;
    Signal<void(sys::error_code, Player *)> m_on_player_spawn,
        m_on_player_about_to_die;
};

/******************************************************************************/

// forces clangd to lint properly
template class PlayerPool<256>;

export auto &get_global_player_pool()
{
    static PlayerPool<256> pool;
    return pool;
}
