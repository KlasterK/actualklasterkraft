module;
#include <atomic>
#include <boost/asio.hpp>
#include <boost/container/small_vector.hpp>
#include <concepts>
#include <memory>
#include <tuple>

export module actualklasterkraft.generic.pubsub;

namespace asio = boost::asio;

export template <typename Signature> class Signal;

template <typename... Args>
    requires(
        (std::is_object_v<Args> && std::default_initializable<Args>) && ...)
class Signal<void(Args...)>
{
    // Обёртка, которая позволяет вызвать handler только один раз
    struct HandlerSlot
    {
        asio::any_completion_handler<void(Args...)> handler;
        std::atomic_bool invoked { false };

        template <typename... A> void try_invoke(A &&...args)
        {
            bool expected = false;
            if (invoked.compare_exchange_strong(expected, true))
            {
                auto h = std::move(handler);
                if (h)
                    std::move(h)(std::forward<A>(args)...);
            }
        }
    };

public:
    Signal() { m_awaiters.reserve(1); }

    Signal(const Signal &) = delete;
    Signal(Signal &&) = default;
    Signal &operator=(const Signal &) = delete;
    Signal &operator=(Signal &&) = default;

    void emit(Args... args)
    {
        decltype(m_awaiters) awaiters;
        std::swap(m_awaiters, awaiters);

        for (auto &slot : awaiters)
        {
            if (slot)
                slot->try_invoke(std::move(args)...);
        }
    }

    template <typename CompletionToken> auto wait(CompletionToken &&token)
    {
        return asio::async_initiate<CompletionToken, void(Args...)>(
            [this](auto completion_handler)
            {
                auto slot = std::make_shared<HandlerSlot>();
                slot->handler = std::move(completion_handler);

                m_awaiters.push_back(slot);

                auto cancel_slot
                    = asio::get_associated_cancellation_slot(slot->handler);
                if (!cancel_slot.is_connected())
                    return;

                cancel_slot.assign(
                    [slot](asio::cancellation_type type) mutable
                    {
                        if (type == asio::cancellation_type::none)
                            return;

                        // Вызываем только один раз (с "отменёнными" аргументами)
                        if constexpr (sizeof...(Args) == 0)
                        {
                            slot->try_invoke();
                        }
                        else
                        {
                            std::tuple<Args...> args { };

                            // Раскомментируй, если первый аргумент — error_code:
                            // std::get<0>(args) = asio::error::operation_aborted;

                            std::apply(
                                [&](auto &&...a)
                                {
                                    slot->try_invoke(
                                        std::forward<decltype(a)>(a)...);
                                },
                                std::move(args));
                        }
                    });
            },
            token);
    }

private:
    boost::container::small_vector<std::shared_ptr<HandlerSlot>, 1> m_awaiters;
};

template class Signal<void()>;