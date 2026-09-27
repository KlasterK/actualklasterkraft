module;
#include <concepts>
#include <cstddef>
#include <tuple>
#include <type_traits>
#include <utility>
export module actualklasterkraft.templates;

export struct TieReturnT
{
};

export TieReturnT TieReturn;

export template <typename... Ts> class InlineTie
{
public:
    InlineTie(Ts &...args)
        : m_unsubstituted_tuple_of_refs { args... }
    {
    }

    template <typename RHS> auto operator=(RHS rhs)
    {
        return assign_impl(std::move(rhs), std::index_sequence_for<Ts...> { });
    }

private:
    static constexpr size_t TieReturnNotFound = sizeof...(Ts),
                            TieReturnDup = sizeof...(Ts) + 1;

    static consteval size_t tie_return_idx()
    {
        size_t idx { }, result { TieReturnNotFound };
        (
            [&]
            {
                if constexpr (std::same_as<TieReturnT, Ts>)
                {
                    if (result == TieReturnNotFound)
                        result = idx;
                    else
                        result = TieReturnDup;
                }
                ++idx;
            }(),
            ...);
        return result;
    }

    static_assert(tie_return_idx() != TieReturnNotFound);
    static_assert(tie_return_idx() != TieReturnDup);

    template <typename RHS, size_t... Is>
    auto assign_impl(RHS rhs, std::index_sequence<Is...>)
    {
        std::remove_reference_t<std::tuple_element_t<tie_return_idx(), RHS>>
            ret;
        std::tie((std::get<Is != tie_return_idx()>(std::tie(ret,
            std::get<Is>(m_unsubstituted_tuple_of_refs))))...) = std::move(rhs);
        return ret;
    }

private:
    std::tuple<Ts &...> m_unsubstituted_tuple_of_refs;
};
