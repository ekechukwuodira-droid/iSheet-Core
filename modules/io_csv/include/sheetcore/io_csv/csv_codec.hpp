#pragma once

#include "sheetcore/storage/storage.hpp"

#include <memory>

namespace sheetcore::io {

class CsvCodec final : public storage::IWorkbookCodec {
public:
    explicit CsvCodec(char delimiter = ',') : delimiter_(delimiter) {}

    std::string_view extension() const noexcept override {
        return delimiter_ == '\t' ? ".tsv" : ".csv";
    }

    Result<std::vector<std::uint8_t>> encode(const Workbook& wb) const override;
    Result<void> decode(const std::vector<std::uint8_t>& bytes, Workbook& wb) const override;

private:
    char delimiter_;
};

inline std::shared_ptr<storage::IWorkbookCodec> createCsvCodec(char delimiter = ',') {
    return std::make_shared<CsvCodec>(delimiter);
}

} // namespace sheetcore::io
