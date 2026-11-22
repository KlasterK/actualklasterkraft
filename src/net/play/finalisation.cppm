module;
#include <boost/asio.hpp>
#include <boost/asio/awaitable.hpp>
#include <boost/asio/experimental/awaitable_operators.hpp>
#include <cstddef>
#include <cstdint>
#include <string_view>
#include <utility>
export module actualklasterkraft.net.play.finalisation;

import actualklasterkraft.generic.pubsub;
import actualklasterkraft.generic.templates;

namespace sys = boost::system;
namespace asio = boost::asio;
using namespace asio::experimental::awaitable_operators;

template <typename T> struct DecayHelper
{
    using Type = T;
    static constexpr bool HasEffect = false;
};

template <typename T> struct DecayHelper<std::tuple<T>>
{
    using Type = T;
    static constexpr bool HasEffect = true;
};

export using FinalisationSignal
    = Signal<void(sys::error_code ec, std::string_view ctx)>;

export template <typename T>
asio::awaitable<std::optional<typename DecayHelper<T>::Type>> awaitable_or_fin(
    FinalisationSignal &fin, asio::awaitable<T> wrappee)
{
    auto variant = co_await (
        fin.wait(asio::as_tuple(asio::use_awaitable)) || std::move(wrappee));
    if (variant.index() == 0)
        co_return std::nullopt;

    if constexpr (DecayHelper<T>::HasEffect)
        co_return std::move(std::get<0>(std::get<1>(variant)));
    else
        co_return std::move(std::get<1>(variant));
}

export template <typename T> struct [[nodiscard]] FinResult
{
    sys::error_code fin_ec { };
    std::string_view fin_ctx { };
    std::optional<T> result { };
};

export template <typename T>
asio::awaitable<FinResult<typename DecayHelper<T>::Type>>
awaitable_or_fin_result(FinalisationSignal &fin, asio::awaitable<T> wrappee)
{
    auto variant = co_await (
        fin.wait(asio::as_tuple(asio::use_awaitable)) || std::move(wrappee));

    if (auto *fin_args = std::get_if<0>(&variant))
    {
        const auto &[ec, ctx] = *fin_args;
        co_return FinResult<T> { ec, ctx, std::nullopt };
    }

    FinResult<T> ret;
    if constexpr (DecayHelper<T>::HasEffect)
        ret.result = std::move(std::get<0>(std::get<1>(variant)));
    else
        ret.result = std::move(std::get<1>(variant));
    co_return ret;
}
