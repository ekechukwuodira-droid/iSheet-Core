#include "sheetcore/io_csv/csv_codec.hpp"

#include <charconv>
#include <string>

namespace sheetcore::io {

namespace {

std::string escapeCsvField(std::string_view field, char delim) {
    bool needsQuotes = field.find(delim) != std::string_view::npos ||
                       field.find('"') != std::string_view::npos ||
                       field.find('\n') != std::string_view::npos ||
                       field.find('\r') != std::string_view::npos;
    if (!needsQuotes) {
        return std::string(field);
    }
    std::string out;
    out.reserve(field.size() + 4);
    out.push_back('"');
    for (char ch : field) {
        if (ch == '"') out.push_back('"');
        out.push_back(ch);
    }
    out.push_back('"');
    return out;
}

CellValue parseCsvCell(std::string_view text) {
    if (text.empty()) return EmptyValue{};
    if (text == "TRUE" || text == "true") return true;
    if (text == "FALSE" || text == "false") return false;
    double d = 0.0;
    const char* b = text.data();
    const char* e = text.data() + text.size();
    auto [ptr, ec] = std::from_chars(b, e, d);
    if (ec == std::errc{} && ptr == e) {
        return d;
    }
    return std::string(text);
}

} // namespace

Result<std::vector<std::uint8_t>> CsvCodec::encode(const Workbook& wb) const {
    auto ids = wb.sheetIds();
    if (ids.empty()) {
        return std::vector<std::uint8_t>{};
    }
    const Worksheet* ws = wb.getSheet(ids.front());
    if (!ws) {
        return std::vector<std::uint8_t>{};
    }
    auto used = ws->usedRange();
    if (!used) {
        return std::vector<std::uint8_t>{};
    }

    std::string out;
    for (RowIndex r = 0; r <= used->end.row; ++r) {
        for (ColIndex c = 0; c <= used->end.col; ++c) {
            if (c > 0) out.push_back(delimiter_);
            out += escapeCsvField(toDisplayString(ws->getValue(CellCoord{r, c})), delimiter_);
        }
        out.push_back('\n');
    }
    return std::vector<std::uint8_t>(out.begin(), out.end());
}

Result<void> CsvCodec::decode(const std::vector<std::uint8_t>& bytes, Workbook& wb) const {
    std::string_view text(reinterpret_cast<const char*>(bytes.data()), bytes.size());
    auto sheetRes = wb.addSheet("Sheet1");
    if (!sheetRes) return sheetRes.error();
    Worksheet* ws = sheetRes.value();

    RowIndex row = 0;
    ColIndex col = 0;
    std::string field;
    bool inQuotes = false;

    auto commitField = [&]() {
        if (!field.empty()) {
            if (field.front() == '=') {
                ws->setFormula(CellCoord{row, col}, field);
            } else {
                ws->setValue(CellCoord{row, col}, parseCsvCell(field));
            }
        }
        field.clear();
    };

    for (std::size_t i = 0; i < text.size(); ++i) {
        char ch = text[i];
        if (inQuotes) {
            if (ch == '"') {
                if (i + 1 < text.size() && text[i + 1] == '"') {
                    field.push_back('"');
                    ++i;
                } else {
                    inQuotes = false;
                }
            } else {
                field.push_back(ch);
            }
        } else {
            if (ch == '"') {
                inQuotes = true;
            } else if (ch == delimiter_) {
                commitField();
                ++col;
            } else if (ch == '\r') {
                continue;
            } else if (ch == '\n') {
                commitField();
                ++row;
                col = 0;
            } else {
                field.push_back(ch);
            }
        }
    }
    if (!field.empty() || col > 0) {
        commitField();
    }
    return Result<void>::ok();
}

} // namespace sheetcore::io
