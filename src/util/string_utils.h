#pragma once

#include <algorithm>
#include <cctype>
#include <cerrno>
#include <climits>
#include <cstdint>
#include <cstdlib>
#include <limits>
#include <string>
#include <vector>

namespace minerva
{
    namespace detail
    {
        inline int safe_tolower(char c) noexcept
        {
            return std::tolower(static_cast<unsigned char>(c));
        }
        inline int safe_toupper(char c) noexcept
        {
            return std::toupper(static_cast<unsigned char>(c));
        }
        inline bool is_ws(char c) noexcept
        {
            switch (c)
            {
            case ' ': case '\t': case '\n': case '\v': case '\f': case '\r':
                return true;
            default:
                return false;
            }
        }

        // RAII guard that restores errno when the scope exits. Lets the
        // try_parse_* helpers use strto*() without polluting the caller's
        // errno on either success or failure.
        struct errno_guard
        {
            int saved;
            errno_guard() noexcept : saved(errno) {}
            ~errno_guard() noexcept { errno = saved; }
            errno_guard(const errno_guard&)            = delete;
            errno_guard& operator=(const errno_guard&) = delete;
        };

        // Trim leading/trailing whitespace and report whether anything is
        // left. Returns the trimmed [begin, end) range as pointers into
        // @p s. On empty/all-ws input, returns false.
        inline bool trim_view(const std::string& s,
                              const char*& begin, const char*& end) noexcept
        {
            const char* b = s.data();
            const char* e = b + s.size();
            while (b < e && is_ws(*b))     ++b;
            while (e > b && is_ws(e[-1])) --e;
            if (b == e) return false;
            begin = b;
            end   = e;
            return true;
        }
    }

    // Case-insensitive comparator suitable for ordered containers.
    struct nocase_compare
    {
        bool operator()(unsigned char c1, unsigned char c2) const noexcept
        {
            return std::tolower(c1) < std::tolower(c2);
        }
    };

    struct ci_less
    {
        bool operator()(const std::string& s1, const std::string& s2) const noexcept
        {
            return std::lexicographical_compare(
                s1.begin(), s1.end(),
                s2.begin(), s2.end(),
                nocase_compare());
        }
    };

    // ---- numeric parsers ---------------------------------------------------
    //
    // All try_parse_* helpers:
    //   - accept and ignore leading and trailing whitespace
    //   - require the rest of the string to be entirely consumed
    //   - leave errno unchanged
    //   - return false on empty / all-whitespace / parse error / overflow

    inline bool try_parse_double(const std::string& s, double& value) noexcept
    {
        const char* b;
        const char* e;
        if (!detail::trim_view(s, b, e)) return false;

        detail::errno_guard guard;
        errno = 0;
        char* end_ptr = nullptr;
        // strtod is given the trimmed, NUL-terminated tail. Since the
        // original string is NUL-terminated and our trim only moved the
        // logical end inward, we copy into a small buffer when the trim
        // chopped trailing whitespace; otherwise we can parse in place.
        std::string tmp(b, e);
        const double result = std::strtod(tmp.c_str(), &end_ptr);
        if (errno == ERANGE)               return false;
        if (end_ptr == tmp.c_str())        return false;
        if (end_ptr != tmp.c_str() + tmp.size()) return false;
        value = result;
        return true;
    }

    inline bool try_parse_float(const std::string& s, float& value) noexcept
    {
        const char* b;
        const char* e;
        if (!detail::trim_view(s, b, e)) return false;

        detail::errno_guard guard;
        errno = 0;
        char* end_ptr = nullptr;
        std::string tmp(b, e);
        const float result = std::strtof(tmp.c_str(), &end_ptr);
        if (errno == ERANGE)               return false;
        if (end_ptr == tmp.c_str())        return false;
        if (end_ptr != tmp.c_str() + tmp.size()) return false;
        value = result;
        return true;
    }

    inline bool try_parse_int(const std::string& s, int& value) noexcept
    {
        const char* b;
        const char* e;
        if (!detail::trim_view(s, b, e)) return false;

        detail::errno_guard guard;
        errno = 0;
        char* end_ptr = nullptr;
        std::string tmp(b, e);
        const long result = std::strtol(tmp.c_str(), &end_ptr, 10);
        if (errno == ERANGE)               return false;
        if (end_ptr == tmp.c_str())        return false;
        if (end_ptr != tmp.c_str() + tmp.size()) return false;
        if (result < std::numeric_limits<int>::min() ||
            result > std::numeric_limits<int>::max())
            return false;
        value = static_cast<int>(result);
        return true;
    }

    inline bool try_parse_int64(const std::string& s, long long& value) noexcept
    {
        const char* b;
        const char* e;
        if (!detail::trim_view(s, b, e)) return false;

        detail::errno_guard guard;
        errno = 0;
        char* end_ptr = nullptr;
        std::string tmp(b, e);
        const long long result = std::strtoll(tmp.c_str(), &end_ptr, 10);
        if (errno == ERANGE)               return false;
        if (end_ptr == tmp.c_str())        return false;
        if (end_ptr != tmp.c_str() + tmp.size()) return false;
        value = result;
        return true;
    }

    inline bool try_parse_uint(const std::string& s, unsigned int& value) noexcept
    {
        const char* b;
        const char* e;
        if (!detail::trim_view(s, b, e)) return false;

        // strtoul silently accepts leading '-' and rolls over. Reject any
        // sign; for parsing into an unsigned, '+' isn't useful either.
        if (*b == '-' || *b == '+') return false;

        detail::errno_guard guard;
        errno = 0;
        char* end_ptr = nullptr;
        std::string tmp(b, e);
        const unsigned long result = std::strtoul(tmp.c_str(), &end_ptr, 10);
        if (errno == ERANGE)               return false;
        if (end_ptr == tmp.c_str())        return false;
        if (end_ptr != tmp.c_str() + tmp.size()) return false;
        if (result > std::numeric_limits<unsigned int>::max())
            return false;
        value = static_cast<unsigned int>(result);
        return true;
    }

    inline bool try_parse_bool(const std::string& s, bool& value) noexcept
    {
        const char* b;
        const char* e;
        if (!detail::trim_view(s, b, e)) return false;

        // Compare lower-cased bytes against a small fixed set; avoid
        // allocating a lowered copy.
        const std::size_t n = static_cast<std::size_t>(e - b);
        auto eq = [&](const char* lit, std::size_t len) noexcept {
            if (n != len) return false;
            for (std::size_t i = 0; i < n; ++i)
            {
                if (detail::safe_tolower(b[i]) != lit[i]) return false;
            }
            return true;
        };
        if (eq("true", 4) || eq("yes", 3) || eq("on", 2) || eq("1", 1))
        {
            value = true;
            return true;
        }
        if (eq("false", 5) || eq("no", 2) || eq("off", 3) || eq("0", 1))
        {
            value = false;
            return true;
        }
        return false;
    }

    // ---- case conversion ---------------------------------------------------

    inline std::string& to_lower_inplace(std::string& s) noexcept
    {
        for (char& c : s) c = static_cast<char>(detail::safe_tolower(c));
        return s;
    }

    inline std::string& to_upper_inplace(std::string& s) noexcept
    {
        for (char& c : s) c = static_cast<char>(detail::safe_toupper(c));
        return s;
    }

    // ---- prefix / suffix / equality ---------------------------------------

    inline bool starts_with(const std::string& s, const std::string& prefix) noexcept
    {
        return s.size() >= prefix.size() &&
               s.compare(0, prefix.size(), prefix) == 0;
    }

    inline bool ends_with(const std::string& s, const std::string& suffix) noexcept
    {
        return s.size() >= suffix.size() &&
               s.compare(s.size() - suffix.size(), suffix.size(), suffix) == 0;
    }

    inline bool ci_equals(const std::string& s1, const std::string& s2) noexcept
    {
        return s1.size() == s2.size() &&
               std::equal(s1.begin(), s1.end(), s2.begin(),
                          [](char a, char b) noexcept {
                              return detail::safe_tolower(a) ==
                                     detail::safe_tolower(b);
                          });
    }

    // ---- trimming ----------------------------------------------------------

    inline std::string& ltrim(std::string& str,
                              const std::string& chars = "\t\n\v\f\r ")
    {
        const auto pos = str.find_first_not_of(chars);
        if (pos == std::string::npos) { str.clear(); return str; }
        str.erase(0, pos);
        return str;
    }

    inline std::string& rtrim(std::string& str,
                              const std::string& chars = "\t\n\v\f\r ")
    {
        const auto pos = str.find_last_not_of(chars);
        if (pos == std::string::npos) { str.clear(); return str; }
        if (pos + 1 < str.size()) str.erase(pos + 1);
        return str;
    }

    inline std::string& trim(std::string& str,
                             const std::string& chars = "\t\n\v\f\r ")
    {
        return ltrim(rtrim(str, chars), chars);
    }

    inline std::string& trim_trailing_newline(std::string& str)
    {
        return rtrim(str, "\r\n");
    }

    // ---- splitting ---------------------------------------------------------

    /**
     * Split @p str on @p delimiter into @p result. By default, empty fields
     * (consecutive delimiters, leading/trailing delimiter) are kept; set
     * @p keep_empty to false to drop them.
     */
    inline void split(const std::string& str, char delimiter,
                      std::vector<std::string>& result,
                      bool keep_empty = true)
    {
        result.clear();
        std::size_t start = 0;
        std::size_t pos = 0;

        while ((pos = str.find(delimiter, start)) != std::string::npos)
        {
            if (keep_empty || pos > start)
            {
                result.emplace_back(str.substr(start, pos - start));
            }
            start = pos + 1;
        }

        if (keep_empty || start < str.size())
        {
            result.emplace_back(str.substr(start));
        }
    }

    // ---- hex parsing -------------------------------------------------------

    /**
     * Parse a chunked-encoding hex size. Accepts up to 16 hex digits,
     * no "0x" prefix, no sign, no whitespace.
     */
    inline bool try_parse_hex(const std::string& hex_str, std::size_t& result) noexcept
    {
        if (hex_str.empty() || hex_str.size() > 16) return false;
        std::size_t acc = 0;
        for (char c : hex_str)
        {
            unsigned digit;
            if      (c >= '0' && c <= '9') digit = static_cast<unsigned>(c - '0');
            else if (c >= 'A' && c <= 'F') digit = static_cast<unsigned>(c - 'A' + 10);
            else if (c >= 'a' && c <= 'f') digit = static_cast<unsigned>(c - 'a' + 10);
            else return false;
            // Length cap above guarantees no shift overflow.
            acc = (acc << 4) | digit;
        }
        result = acc;
        return true;
    }
}
