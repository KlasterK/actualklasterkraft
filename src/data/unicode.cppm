module;
#include <array>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string_view>
export module actualklasterkraft.data.unicode;

export namespace unicode
{
    size_t shallow_count_length(std::string_view s)
    {
        size_t len { };
        for (char c : s)
            len += (c & 0b11000000) != 0b10000000;
        return len;
    }

    std::optional<size_t> verify_count_length(std::string_view s)
    {
        size_t len { };
        unsigned remaining_bytes { };
        for (char c : s)
        {
            int ones = std::countl_one(static_cast<uint8_t>(c));
            if (ones > 4)
                return std::nullopt;

            if (ones == 0)
            {
                if (remaining_bytes > 0)
                    return std::nullopt;
                ++len;
            }
            else if (ones == 1)
            {
                if (remaining_bytes-- == 0)
                    return std::nullopt;
                continue;
            }
            else if (ones > 1)
            {
                if (remaining_bytes > 0)
                    return std::nullopt;
                remaining_bytes = ones - 1;
                ++len;
            }
        }
        return len;
    }

    constexpr std::array<std::string_view, 25> Whitespaces {
        " ",
        "\t",
        "\v",
        "\r",
        "\n",
        "\f",
        "\xC2\x85", // NEL
        "\xC2\xA0", // NBSP
        "\xE1\x9A\x80", // OGHAM SPACE MARK
        "\xE2\x80\x80", // EN QUAD
        "\xE2\x80\x81", // EM QUAD
        "\xE2\x80\x82", // EN SPACE
        "\xE2\x80\x83", // EM SPACE
        "\xE2\x80\x84", // THREE-PER-EM SPACE
        "\xE2\x80\x85", // FOUR-PER-EM SPACE
        "\xE2\x80\x86", // SIX-PER-EM SPACE
        "\xE2\x80\x87", // FIGURE SPACE
        "\xE2\x80\x88", // PUNCTUATION SPACE
        "\xE2\x80\x89", // THIN SPACE
        "\xE2\x80\x8A", // HAIR SPACE
        "\xE2\x80\xA8", // LINE SEPARATOR
        "\xE2\x80\xA9", // PARAGRAPH SEPARATOR
        "\xE2\x80\xAF", // NARROW NO-BREAK SPACE
        "\xE2\x81\x9F", // MEDIUM MATHEMATICAL SPACE
        "\xE3\x80\x80", // IDEOGRAPHIC SPACE
    };

    /// Returns an `s` subrange containing the first appeared whitespace
    /// or an empty subrange at `s.end()` if none is found.
    std::string_view find_first_whitespace(std::string_view s)
    {
        while (!s.empty())
        {
            for (const auto &ws : Whitespaces)
            {
                if (s.starts_with(ws))
                    return s.substr(0, ws.size());
            }
            s.remove_prefix(1);
        }
        return { s.end(), 0 };
    }

    /// Returns an iterator pointing to the first non-whitespace character in
    /// `s`  or `s.end()` if the whole string is whitespaces.
    std::string_view::iterator find_first_not_whitespace(std::string_view s)
    {
        while (!s.empty())
        {
            bool not_a_ws = true;
            for (const auto &ws : Whitespaces)
            {
                if (s.starts_with(ws))
                {
                    s.remove_prefix(ws.length());
                    not_a_ws = false;
                    break;
                }
            }
            if (not_a_ws)
                return s.begin();
        }
        return s.end();
    }

    auto lsplit_once(std::string_view s)
    {
        auto first_ws = find_first_whitespace(s);
        auto after_ws = find_first_not_whitespace({ first_ws.end(), s.end() });
        return std::array {
            std::string_view { s.begin(), first_ws.begin() },
            std::string_view { after_ws, s.end() },
        };
    }
}
