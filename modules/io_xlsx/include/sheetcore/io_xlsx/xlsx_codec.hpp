#pragma once

#include "sheetcore/storage/storage.hpp"

#include <memory>

namespace sheetcore::io {

class XlsxCodec final : public storage::IWorkbookCodec {
public:
    std::string_view extension() const noexcept override { return ".xlsx"; }

    Result<std::vector<std::uint8_t>> encode(const Workbook& wb) const override;
    Result<void> decode(const std::vector<std::uint8_t>& bytes, Workbook& wb) const override;
};

inline std::shared_ptr<storage::IWorkbookCodec> createXlsxCodec() {
    return std::make_shared<XlsxCodec>();
}

} // namespace sheetcore::io
