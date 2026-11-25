#pragma once

#include "sheetcore/core/result.hpp"
#include "sheetcore/document/workbook.hpp"

#include <cstdint>
#include <filesystem>
#include <memory>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace sheetcore::storage {

class IWorkbookCodec {
public:
    virtual ~IWorkbookCodec() = default;
    virtual std::string_view extension() const noexcept = 0; // e.g., ".isht", ".csv"
    virtual Result<std::vector<std::uint8_t>> encode(const Workbook& wb) const = 0;
    virtual Result<void> decode(const std::vector<std::uint8_t>& bytes, Workbook& wb) const = 0;
};

class StorageManager {
public:
    void registerCodec(std::shared_ptr<IWorkbookCodec> codec);
    const IWorkbookCodec* findCodec(std::string_view extension) const;

    Result<void> save(const Workbook& wb, const std::filesystem::path& path) const;
    Result<void> load(Workbook& wb, const std::filesystem::path& path) const;

    static Result<void> writeAtomic(const std::filesystem::path& path,
                                    const std::vector<std::uint8_t>& bytes);
    static Result<std::vector<std::uint8_t>> readAllBytes(const std::filesystem::path& path);

private:
    static std::string normalizeExt(std::string_view ext);
    std::unordered_map<std::string, std::shared_ptr<IWorkbookCodec>> codecs_;
};

} // namespace sheetcore::storage
