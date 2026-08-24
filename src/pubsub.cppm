module;
#include <boost/asio.hpp>
#include <boost/asio/error.hpp>
#include <boost/asio/experimental/awaitable_operators.hpp>
#include <boost/system/detail/errc.hpp>
#include <boost/system/detail/error_code.hpp>
#include <coroutine>
#include <print>
#include <tuple>
#include <vector>
export module actualklasterkraft.pubsub;

namespace asio = boost::asio;

export template <typename Signature> class WeakSignal;

export template <typename... Args> class WeakSignal<void(Args...)>
{
public:
    WeakSignal(asio::io_context &io)
        : m_io(io)
    {
    }

    void emit(Args... args)
    {
        for (auto &awaiter : m_awaiters)
        {
            if (awaiter == nullptr)
                continue;

            asio::post(m_io, [aw = std::move(awaiter), args...] mutable
                { std::move(aw)(args...); });
        }
        m_awaiters.clear();
    }

    template <typename CompletionToken> auto wait(CompletionToken &&token)
    {
        return asio::async_initiate<CompletionToken, void(Args...)>(
            [this](auto completion_handler)
            {
                auto &awaiter
                    = m_awaiters.emplace_back(std::move(completion_handler));
                auto slot = asio::get_associated_cancellation_slot(awaiter);
                if (slot.is_connected())
                {
                    slot.assign(
                        [this, idx = m_awaiters.size() - 1](
                            asio::cancellation_type type)
                        {
                            if (type == asio::cancellation_type::none)
                                return;

                            asio::post(m_io,
                                [aw = std::move(m_awaiters[idx])] mutable
                                {
                                    std::tuple<Args...> args;
                                    std::get<0>(args)
                                        = asio::error::operation_aborted;

                                    std::apply([&](auto &&...args)
                                        { std::move(aw)(args...); }, args);
                                });

                            m_awaiters[idx] = nullptr;
                        });
                }
            },
            token);
    }

private:
    std::vector<asio::any_completion_handler<void(Args...)>> m_awaiters;
    asio::io_context &m_io;
};
