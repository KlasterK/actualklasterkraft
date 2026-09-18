module;
#include <boost/asio.hpp>
#include <concepts>
#include <tuple>
#include <vector>
export module actualklasterkraft.pubsub;

namespace asio = boost::asio;

export template <typename Signature> class Signal;

template <typename... Args>
    requires(
        (std::is_object_v<Args> && std::default_initializable<Args>) && ...)
class Signal<void(Args...)>
{
public:
    Signal(asio::any_io_executor io)
        : m_io(io)
    {
    }

    Signal(const Signal &) = delete;
    Signal(Signal &&) = default;
    Signal &operator=(const Signal &) = delete;
    Signal &operator=(Signal &&) = default;

    void emit(Args... args)
    {
        // We empty the member vector moving its contents to the stack vector
        // so that awaiters could call wait() without problems in themselves
        decltype(m_awaiters) awaiters;
        std::swap(m_awaiters, awaiters);

        for (auto &awaiter : awaiters)
        {
            if (awaiter == nullptr)
                continue;
            awaiter(args...);
        }
    }

    template <typename CompletionToken> auto wait(CompletionToken &&token)
    {
        return asio::async_initiate<CompletionToken, void(Args...)>(
            [this](auto completion_handler)
            {
                auto &awaiter
                    = m_awaiters.emplace_back(std::move(completion_handler));

                auto slot = asio::get_associated_cancellation_slot(awaiter);
                if (!slot.is_connected())
                    return;

                slot.assign(
                    [this, idx = m_awaiters.size() - 1](
                        asio::cancellation_type type)
                    {
                        if (!type)
                            return;

                        std::tuple<Args...> args;
                        std::get<0>(args) = asio::error::operation_aborted;
                        std::apply([&](auto &&...args)
                            { m_awaiters[idx](std::move(args)...); }, args);

                        m_awaiters[idx] = nullptr;
                    });
            },
            token);
    }

private:
    std::vector<asio::any_completion_handler<void(Args...)>> m_awaiters;
    asio::any_io_executor m_io;
};
