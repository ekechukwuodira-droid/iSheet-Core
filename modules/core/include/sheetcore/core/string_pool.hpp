#pragma once

#include <cstdint>
#include <deque>
#include <string>
#include <string_view>
#include <unordered_map>

namespace sheetcore {

using StringId = std::uint32_t;
inline constexpr StringId kEmptyStringId = 0;

class StringPool {
public:
    StringPool();

    StringId intern(std::string_view text);
    std::string_view get(StringId id) const noexcept;
    std::size_t size() const noexcept { return strings_.size(); }

private:
    struct StringHash {
        using is_transparent = void;
        std::size_t operator()(std::string_view sv) const noexcept {
            return std::hash<std::string_view>{}(sv);
        }
    };

    std::deque<std::string> strings_;
    std::unordered_map<std::string_view, StringId, StringHash, std::equal_to<>> index_;
};

} // namespace sheetcore
