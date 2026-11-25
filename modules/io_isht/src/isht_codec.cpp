#include "sheetcore/io_isht/isht_codec.hpp"

// Reuse iDoc's portable little-endian codec & CRC-64/XZ integrity primitives directly
#include "idoc/container/byteorder.hpp"
#include "idoc/container/crc64.hpp"

#include <cstring>
#include <stdexcept>

namespace sheetcore::io {

namespace {

constexpr std::uint8_t kMagic[4] = {'I', 'S', 'H', 'T'};
constexpr std::uint16_t kFormatVersion = 1;

enum class ValueTag : std::uint8_t {
    Empty = 0,
    Number = 1,
    String = 2,
    Boolean = 3,
    DateTime = 4,
    Error = 5
};

void writeF64(std::vector<std::uint8_t>& buf, double v) {
    std::uint64_t bits = 0;
    static_assert(sizeof(bits) == sizeof(v));
    std::memcpy(&bits, &v, sizeof(bits));
    idoc::byteorder::write_u64(buf, bits);
}

double readF64(idoc::byteorder::Reader& r) {
    std::uint64_t bits = r.read_u64();
    double v = 0.0;
    std::memcpy(&v, &bits, sizeof(v));
    return v;
}

} // namespace

Result<std::vector<std::uint8_t>> IshtCodec::encode(const Workbook& wb) const {
    using namespace idoc::byteorder;
    std::vector<std::uint8_t> buf;
    buf.reserve(4096);

    // Header
    write_bytes(buf, kMagic, 4);
    write_u16(buf, kFormatVersion);
    write_u16(buf, 0); // reserved flags
    write_u8(buf, static_cast<std::uint8_t>(wb.calculationMode()));

    // Named ranges
    const auto& names = wb.namedRanges().all();
    write_u32(buf, static_cast<std::uint32_t>(names.size()));
    for (const auto& [_, nr] : names) {
        write_string(buf, nr.name);
        write_u32(buf, nr.scopeSheetId);
        write_string(buf, nr.expression);
    }

    // Sheets
    auto ids = wb.sheetIds();
    write_u32(buf, static_cast<std::uint32_t>(ids.size()));
    for (SheetId sid : ids) {
        const Worksheet* ws = wb.getSheet(sid);
        write_string(buf, ws->name());
        write_u32(buf, ws->frozenPanes().rows);
        write_u32(buf, ws->frozenPanes().cols);
        write_u8(buf, ws->protection().protectedSheet ? 1 : 0);

        // Collect cells
        struct CellEntry {
            CellCoord coord;
            CellRecord rec;
        };
        std::vector<CellEntry> cells;
        cells.reserve(ws->cellCount());
        ws->forEachCell([&](CellCoord c, const CellRecord& rec) {
            cells.push_back(CellEntry{c, rec});
        });

        write_u32(buf, static_cast<std::uint32_t>(cells.size()));
        for (const auto& entry : cells) {
            write_u32(buf, entry.coord.row);
            write_u32(buf, entry.coord.col);
            write_u32(buf, entry.rec.styleId);

            // Formula
            write_u8(buf, entry.rec.formulaText.has_value() ? 1 : 0);
            if (entry.rec.formulaText.has_value()) {
                write_string(buf, *entry.rec.formulaText);
            }

            // Cached/literal value
            if (isEmpty(entry.rec.value)) {
                write_u8(buf, static_cast<std::uint8_t>(ValueTag::Empty));
            } else if (const auto* d = std::get_if<double>(&entry.rec.value)) {
                write_u8(buf, static_cast<std::uint8_t>(ValueTag::Number));
                writeF64(buf, *d);
            } else if (const auto* s = std::get_if<std::string>(&entry.rec.value)) {
                write_u8(buf, static_cast<std::uint8_t>(ValueTag::String));
                write_string(buf, *s);
            } else if (const auto* b = std::get_if<bool>(&entry.rec.value)) {
                write_u8(buf, static_cast<std::uint8_t>(ValueTag::Boolean));
                write_u8(buf, *b ? 1 : 0);
            } else if (const auto* dt = std::get_if<DateTimeValue>(&entry.rec.value)) {
                write_u8(buf, static_cast<std::uint8_t>(ValueTag::DateTime));
                writeF64(buf, dt->serial);
            } else if (const auto* err = std::get_if<ErrorValue>(&entry.rec.value)) {
                write_u8(buf, static_cast<std::uint8_t>(ValueTag::Error));
                write_u8(buf, static_cast<std::uint8_t>(err->type));
            }
        }
    }

    // Trailing CRC-64/XZ integrity checksum (computed via iDoc::crc64)
    std::uint64_t checksum = idoc::crc64(buf);
    write_u64(buf, checksum);
    return buf;
}

Result<void> IshtCodec::decode(const std::vector<std::uint8_t>& bytes, Workbook& wb) const {
    using namespace idoc::byteorder;
    if (bytes.size() < 16) {
        return Error{ErrorCode::IOError, "Invalid .isht file: too small"};
    }

    const std::size_t payloadLen = bytes.size() - 8;
    Reader crcReader(bytes.data(), bytes.size(), payloadLen);
    std::uint64_t storedCrc = crcReader.read_u64();
    std::uint64_t computedCrc = idoc::crc64(bytes.data(), payloadLen);
    if (storedCrc != computedCrc) {
        return Error{ErrorCode::IOError, "Corrupt .isht file: CRC-64 checksum mismatch"};
    }

    try {
        Reader r(bytes.data(), payloadLen, 0);
        auto magic = r.read_bytes(4);
        if (std::memcmp(magic.data(), kMagic, 4) != 0) {
            return Error{ErrorCode::IOError, "Invalid .isht signature"};
        }
        std::uint16_t version = r.read_u16();
        if (version != kFormatVersion) {
            return Error{ErrorCode::Unsupported, "Unsupported .isht version"};
        }
        (void)r.read_u16(); // flags
        wb.setCalculationMode(static_cast<CalculationMode>(r.read_u8()));

        std::uint32_t nameCount = r.read_u32();
        for (std::uint32_t i = 0; i < nameCount; ++i) {
            NamedRangeDef nr;
            nr.name = r.read_string();
            nr.scopeSheetId = r.read_u32();
            nr.expression = r.read_string();
            wb.namedRanges().define(std::move(nr));
        }

        std::uint32_t sheetCount = r.read_u32();
        for (std::uint32_t s = 0; s < sheetCount; ++s) {
            std::string name = r.read_string();
            auto wsRes = wb.addSheet(std::move(name));
            if (!wsRes) return wsRes.error();
            Worksheet* ws = wsRes.value();

            FrozenPanes fp;
            fp.rows = r.read_u32();
            fp.cols = r.read_u32();
            ws->setFrozenPanes(fp);

            SheetProtection prot;
            prot.protectedSheet = r.read_u8() != 0;
            ws->setProtection(prot);

            std::uint32_t cellCount = r.read_u32();
            for (std::uint32_t c = 0; c < cellCount; ++c) {
                RowIndex row = r.read_u32();
                ColIndex col = r.read_u32();
                StyleId styleId = r.read_u32();
                bool hasFormula = r.read_u8() != 0;
                std::string formulaText = hasFormula ? r.read_string() : std::string{};

                ValueTag tag = static_cast<ValueTag>(r.read_u8());
                CellValue val = EmptyValue{};
                switch (tag) {
                    case ValueTag::Empty: val = EmptyValue{}; break;
                    case ValueTag::Number: val = readF64(r); break;
                    case ValueTag::String: val = r.read_string(); break;
                    case ValueTag::Boolean: val = (r.read_u8() != 0); break;
                    case ValueTag::DateTime: val = DateTimeValue{readF64(r)}; break;
                    case ValueTag::Error: val = ErrorValue{static_cast<ErrorType>(r.read_u8())}; break;
                }

                CellCoord coord{row, col};
                if (hasFormula) {
                    ws->setFormula(coord, std::move(formulaText));
                    ws->setCachedValue(coord, std::move(val));
                } else if (!isEmpty(val)) {
                    ws->setValue(coord, std::move(val));
                }
                if (styleId != kDefaultStyleId) {
                    ws->setStyle(coord, styleId);
                }
            }
        }
        wb.markSaved();
        return Result<void>::ok();
    } catch (const std::exception& ex) {
        return Error{ErrorCode::IOError, std::string("Failed to decode .isht: ") + ex.what()};
    }
}

} // namespace sheetcore::io
