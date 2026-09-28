module;
#include <boost/system.hpp>
#include <exception>
#include <print>
export module actualklasterkraft.generic.completiontokens;

export const auto detached_log_exceptions_token = [](std::exception_ptr exc_ptr)
{
    if (!exc_ptr)
        return;

    try
    {
        std::rethrow_exception(exc_ptr);
    }
    catch (const std::exception &exc)
    {
        std::println(
            "Unhandled exception in a detached async operation!\nWhat: {}",
            exc.what());
    }
};

export const auto detached_rethrow_exceptions_token
    = [](std::exception_ptr exc_ptr)
{
    if (!exc_ptr)
        return;

    std::rethrow_exception(exc_ptr);
};
