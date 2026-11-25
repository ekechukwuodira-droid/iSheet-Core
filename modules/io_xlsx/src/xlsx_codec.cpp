#include "sheetcore/io_xlsx/xlsx_codec.hpp"
#include "idoc/container/byteorder.hpp"

#include <algorithm>
#include <charconv>
#include <map>
#include <sstream>
#include <unordered_map>

namespace sheetcore::io {

namespace {

std::uint32_t crc32(const std::uint8_t* data, std::size_t len) {
    std::uint32_t crc = 0xFFFFFFFFu;
    for (std::size_t i = 0; i < len; ++i) {
        crc ^= data[i];
        for (int b = 0; b < 8; ++b) {
            crc = (crc >> 1) ^ (0xEDB88320u & (0u - (crc & 1u)));
        }
    }
    return ~crc;
}

struct ZipEntry {
    std::string name;
    std::vector<std::uint8_t> data;
};

std::vector<std::uint8_t> writeZipStore(const std::vector<ZipEntry>& entries) {
    using namespace idoc::byteorder;
    std::vector<std::uint8_t> out;
    struct CentralRecord {
        std::string name;
        std::uint32_t crc;
        std::uint32_t size;
        std::uint32_t localOffset;
    };
    std::vector<CentralRecord> records;

    for (const auto& e : entries) {
        std::uint32_t offset = static_cast<std::uint32_t>(out.size());
        std::uint32_t crc = crc32(e.data.data(), e.data.size());
        std::uint32_t sz = static_cast<std::uint32_t>(e.data.size());

        // Local file header
        write_u32(out, 0x04034B50u);
        write_u16(out, 20); // version needed
        write_u16(out, 0);  // flags
        write_u16(out, 0);  // compression: STORE
        write_u16(out, 0);  // mod time
        write_u16(out, 0);  // mod date
        write_u32(out, crc);
        write_u32(out, sz);
        write_u32(out, sz);
        write_u16(out, static_cast<std::uint16_t>(e.name.size()));
        write_u16(out, 0);  // extra len
        write_bytes(out, reinterpret_cast<const std::uint8_t*>(e.name.data()), e.name.size());
        write_bytes(out, e.data);

        records.push_back(CentralRecord{e.name, crc, sz, offset});
    }

    std::uint32_t cdOffset = static_cast<std::uint32_t>(out.size());
    for (const auto& rec : records) {
        write_u32(out, 0x02014B50u);
        write_u16(out, 20);
        write_u16(out, 20);
        write_u16(out, 0);
        write_u16(out, 0); // STORE
        write_u16(out, 0);
        write_u16(out, 0);
        write_u32(out, rec.crc);
        write_u32(out, rec.size);
        write_u32(out, rec.size);
        write_u16(out, static_cast<std::uint16_t>(rec.name.size()));
        write_u16(out, 0);
        write_u16(out, 0);
        write_u16(out, 0);
        write_u16(out, 0);
        write_u32(out, 0);
        write_u32(out, rec.localOffset);
        write_bytes(out, reinterpret_cast<const std::uint8_t*>(rec.name.data()), rec.name.size());
    }
    std::uint32_t cdSize = static_cast<std::uint32_t>(out.size()) - cdOffset;

    // End of central directory
    write_u32(out, 0x06054B50u);
    write_u16(out, 0);
    write_u16(out, 0);
    write_u16(out, static_cast<std::uint16_t>(records.size()));
    write_u16(out, static_cast<std::uint16_t>(records.size()));
    write_u32(out, cdSize);
    write_u32(out, cdOffset);
    write_u16(out, 0);

    return out;
}

class BitReader final {
public:
    BitReader(const std::uint8_t* data, std::size_t size) : data_(data), size_(size) {}
    std::uint32_t bits(unsigned n) {
        while (count_ < n) {
            if (pos_ >= size_) throw std::runtime_error("deflate stream truncated");
            buffer_ |= std::uint32_t(data_[pos_++]) << count_;
            count_ += 8;
        }
        const std::uint32_t v = buffer_ & ((n == 32) ? 0xFFFFFFFFu : ((1u << n) - 1u));
        buffer_ >>= n;
        count_ -= n;
        return v;
    }
    void align() { buffer_ = 0; count_ = 0; }
private:
    const std::uint8_t* data_;
    std::size_t size_, pos_ = 0;
    std::uint32_t buffer_ = 0;
    unsigned count_ = 0;
};

struct Huffman final {
    struct Node { int child[2] = {-1, -1}; int symbol = -1; };
    std::vector<Node> nodes;
    void build(const std::vector<unsigned>& lengths) {
        nodes.clear();
        nodes.push_back({});
        unsigned maxLen = 0;
        for (unsigned n : lengths) maxLen = std::max(maxLen, n);
        std::vector<unsigned> count(maxLen + 1, 0), next(maxLen + 1, 0);
        for (unsigned n : lengths) if (n) ++count[n];
        unsigned code = 0;
        for (unsigned b = 1; b <= maxLen; ++b) {
            code = (code + count[b - 1]) << 1;
            next[b] = code;
        }
        auto reverseBits = [](unsigned v, unsigned n) {
            unsigned r = 0;
            while (n--) { r = (r << 1) | (v & 1u); v >>= 1; }
            return r;
        };
        for (std::size_t sym = 0; sym < lengths.size(); ++sym) {
            const unsigned len = lengths[sym];
            if (!len) continue;
            const unsigned c = reverseBits(next[len]++, len);
            int node = 0;
            for (unsigned b = 0; b < len; ++b) {
                const unsigned bit = (c >> b) & 1u;
                if (nodes[node].child[bit] < 0) {
                    nodes[node].child[bit] = static_cast<int>(nodes.size());
                    nodes.push_back({});
                }
                node = nodes[node].child[bit];
            }
            nodes[node].symbol = static_cast<int>(sym);
        }
    }
    int decode(BitReader& br) const {
        int node = 0;
        for (;;) {
            if (node < 0 || node >= static_cast<int>(nodes.size())) {
                throw std::runtime_error("invalid deflate Huffman tree");
            }
            if (nodes[node].symbol >= 0) return nodes[node].symbol;
            node = nodes[node].child[br.bits(1)];
        }
    }
};

std::vector<std::uint8_t> inflateRaw(const std::uint8_t* data, std::size_t size, std::size_t expectedSize) {
    static const unsigned lenBase[] = {3,4,5,6,7,8,9,10,11,13,15,17,19,23,27,31,35,43,51,59,67,83,99,115,131,163,195,227,258};
    static const unsigned lenExtra[] = {0,0,0,0,0,0,0,0,1,1,1,1,2,2,2,2,3,3,3,3,4,4,4,4,5,5,5,5,0};
    static const unsigned distBase[] = {1,2,3,4,5,7,9,13,17,25,33,49,65,97,129,193,257,385,513,769,1025,1537,2049,3073,4097,6145,8193,12289,16385,24577};
    static const unsigned distExtra[] = {0,0,0,0,1,1,2,2,3,3,4,4,5,5,6,6,7,7,8,8,9,9,10,10,11,11,12,12,13,13};
    constexpr std::size_t kMaxExpansion = 1032;
    BitReader br(data, size);
    std::vector<std::uint8_t> out;
    out.reserve(std::min(expectedSize, size * kMaxExpansion));

    auto makeFixed = []() {
        Huffman lit, dist;
        std::vector<unsigned> ll(288), dd(32, 5);
        for (unsigned i = 0; i <= 143; ++i) ll[i] = 8;
        for (unsigned i = 144; i <= 255; ++i) ll[i] = 9;
        for (unsigned i = 256; i <= 279; ++i) ll[i] = 7;
        for (unsigned i = 280; i < 288; ++i) ll[i] = 8;
        lit.build(ll);
        dist.build(dd);
        return std::pair<Huffman, Huffman>{std::move(lit), std::move(dist)};
    };

    bool isFinal = false;
    while (!isFinal) {
        isFinal = br.bits(1) != 0;
        const unsigned type = br.bits(2);
        if (type == 0) {
            br.align();
            const unsigned len = br.bits(16), nlen = br.bits(16);
            if ((len ^ 0xFFFFu) != nlen) throw std::runtime_error("invalid deflate stored block");
            if (len > expectedSize - out.size()) throw std::runtime_error("deflate output exceeds declared size");
            for (unsigned i = 0; i < len; ++i) out.push_back(static_cast<std::uint8_t>(br.bits(8)));
            continue;
        }
        if (type == 3) throw std::runtime_error("reserved deflate block type");
        Huffman lit, dist;
        if (type == 1) {
            auto p = makeFixed();
            lit = std::move(p.first);
            dist = std::move(p.second);
        } else {
            const unsigned hlit = br.bits(5) + 257, hdist = br.bits(5) + 1, hclen = br.bits(4) + 4;
            static const unsigned order[] = {16,17,18,0,8,7,9,6,10,5,11,4,12,3,13,2,14,1,15};
            std::vector<unsigned> cl(19, 0);
            for (unsigned i = 0; i < hclen; ++i) cl[order[i]] = br.bits(3);
            Huffman ch;
            ch.build(cl);
            std::vector<unsigned> lengths;
            lengths.reserve(hlit + hdist);
            while (lengths.size() < hlit + hdist) {
                const int sym = ch.decode(br);
                if (sym <= 15) lengths.push_back(static_cast<unsigned>(sym));
                else if (sym == 16) {
                    if (lengths.empty()) throw std::runtime_error("deflate repeat with no previous length");
                    const unsigned rep = br.bits(2) + 3, v = lengths.back();
                    for (unsigned i = 0; i < rep && lengths.size() < hlit + hdist; ++i) lengths.push_back(v);
                } else if (sym == 17) {
                    const unsigned rep = br.bits(3) + 3;
                    for (unsigned i = 0; i < rep && lengths.size() < hlit + hdist; ++i) lengths.push_back(0);
                } else if (sym == 18) {
                    const unsigned rep = br.bits(7) + 11;
                    for (unsigned i = 0; i < rep && lengths.size() < hlit + hdist; ++i) lengths.push_back(0);
                } else throw std::runtime_error("invalid deflate code-length symbol");
            }
            lit.build(std::vector<unsigned>(lengths.begin(), lengths.begin() + hlit));
            dist.build(std::vector<unsigned>(lengths.begin() + hlit, lengths.end()));
        }
        for (;;) {
            const int sym = lit.decode(br);
            if (sym < 256) {
                if (out.size() >= expectedSize) throw std::runtime_error("deflate output exceeds declared size");
                out.push_back(static_cast<std::uint8_t>(sym));
                continue;
            }
            if (sym == 256) break;
            if (sym < 257 || sym > 285) throw std::runtime_error("invalid deflate length symbol");
            const unsigned li = static_cast<unsigned>(sym - 257);
            const unsigned length = lenBase[li] + br.bits(lenExtra[li]);
            const int dsym = dist.decode(br);
            if (dsym < 0 || dsym >= 30) throw std::runtime_error("invalid deflate distance symbol");
            const unsigned distance = distBase[dsym] + br.bits(distExtra[dsym]);
            if (distance == 0 || distance > out.size()) throw std::runtime_error("invalid deflate distance");
            if (length > expectedSize - out.size()) throw std::runtime_error("deflate output exceeds declared size");
            for (unsigned i = 0; i < length; ++i) out.push_back(out[out.size() - distance]);
        }
    }
    if (out.size() != expectedSize) throw std::runtime_error("deflate output size mismatch");
    return out;
}

Result<std::unordered_map<std::string, std::string>> readZipStore(const std::vector<std::uint8_t>& bytes) {
    using namespace idoc::byteorder;
    std::unordered_map<std::string, std::string> files;
    constexpr std::size_t kMaxEntryBytes = 256ull * 1024ull * 1024ull;
    try {
        // Locate End of Central Directory (EOCD) so data-descriptor ZIPs from Excel work reliably
        std::size_t eocdPos = std::string::npos;
        const std::size_t searchStart = bytes.size() > 65557 ? bytes.size() - 65557 : 0;
        for (std::size_t i = bytes.size() >= 22 ? bytes.size() - 22 : 0; ; --i) {
            if (bytes.size() >= i + 4 &&
                bytes[i] == 0x50 && bytes[i + 1] == 0x4B && bytes[i + 2] == 0x05 && bytes[i + 3] == 0x06) {
                eocdPos = i;
                break;
            }
            if (i == searchStart) break;
        }
        if (eocdPos == std::string::npos) {
            return Error{ErrorCode::IOError, "Invalid XLSX/ZIP: missing EOCD"};
        }
        Reader eocd(bytes.data(), bytes.size(), eocdPos + 4);
        (void)eocd.read_u16(); // disk
        (void)eocd.read_u16(); // cd disk
        (void)eocd.read_u16(); // entries on disk
        std::uint16_t totalEntries = eocd.read_u16();
        (void)eocd.read_u32(); // cd size
        std::uint32_t cdOffset = eocd.read_u32();

        Reader cd(bytes.data(), bytes.size(), cdOffset);
        for (std::uint16_t idx = 0; idx < totalEntries; ++idx) {
            if (cd.read_u32() != 0x02014B50u) {
                return Error{ErrorCode::IOError, "Invalid ZIP central directory signature"};
            }
            (void)cd.read_u16(); // ver made
            (void)cd.read_u16(); // ver needed
            (void)cd.read_u16(); // flags
            std::uint16_t method = cd.read_u16();
            (void)cd.read_u16(); // time
            (void)cd.read_u16(); // date
            (void)cd.read_u32(); // crc
            std::uint32_t compSz = cd.read_u32();
            std::uint32_t uncompSz = cd.read_u32();
            std::uint16_t nameLen = cd.read_u16();
            std::uint16_t extraLen = cd.read_u16();
            std::uint16_t commentLen = cd.read_u16();
            (void)cd.read_u16(); // disk start
            (void)cd.read_u16(); // internal attrs
            (void)cd.read_u32(); // external attrs
            std::uint32_t localOffset = cd.read_u32();
            auto nameBytes = cd.read_bytes(nameLen);
            std::string name(reinterpret_cast<const char*>(nameBytes.data()), nameBytes.size());
            if (extraLen > 0) (void)cd.read_bytes(extraLen);
            if (commentLen > 0) (void)cd.read_bytes(commentLen);

            if (uncompSz > kMaxEntryBytes) {
                return Error{ErrorCode::IOError, "XLSX entry exceeds safety size limit: " + name};
            }

            Reader loc(bytes.data(), bytes.size(), localOffset);
            if (loc.read_u32() != 0x04034B50u) {
                return Error{ErrorCode::IOError, "Invalid ZIP local header"};
            }
            loc.pos += 22; // skip fixed fields
            std::uint16_t lNameLen = loc.read_u16();
            std::uint16_t lExtraLen = loc.read_u16();
            loc.pos += static_cast<std::size_t>(lNameLen) + static_cast<std::size_t>(lExtraLen);
            auto compPayload = loc.read_bytes(compSz);

            std::vector<std::uint8_t> raw;
            if (method == 0) {
                raw = std::move(compPayload);
            } else if (method == 8) {
                raw = inflateRaw(compPayload.data(), compPayload.size(), uncompSz);
            } else {
                return Error{ErrorCode::Unsupported, "Unsupported ZIP compression method in XLSX: " + name};
            }
            files.emplace(std::move(name), std::string(reinterpret_cast<const char*>(raw.data()), raw.size()));
        }
    } catch (const std::exception& ex) {
        return Error{ErrorCode::IOError, std::string("Invalid XLSX/ZIP stream: ") + ex.what()};
    }
    return files;
}

std::string xmlEscape(std::string_view s) {
    std::string out;
    out.reserve(s.size());
    for (char c : s) {
        switch (c) {
            case '&': out += "&amp;"; break;
            case '<': out += "&lt;"; break;
            case '>': out += "&gt;"; break;
            case '"': out += "&quot;"; break;
            case '\'': out += "&apos;"; break;
            default: out.push_back(c); break;
        }
    }
    return out;
}

std::string xmlUnescape(std::string_view s) {
    std::string out;
    out.reserve(s.size());
    for (std::size_t i = 0; i < s.size(); ++i) {
        if (s[i] == '&') {
            if (s.substr(i, 5) == "&amp;") { out.push_back('&'); i += 4; continue; }
            if (s.substr(i, 4) == "&lt;") { out.push_back('<'); i += 3; continue; }
            if (s.substr(i, 4) == "&gt;") { out.push_back('>'); i += 3; continue; }
            if (s.substr(i, 6) == "&quot;") { out.push_back('"'); i += 5; continue; }
            if (s.substr(i, 6) == "&apos;") { out.push_back('\''); i += 5; continue; }
        }
        out.push_back(s[i]);
    }
    return out;
}

std::string getAttr(std::string_view tag, std::string_view attrName) {
    std::string key = std::string(attrName) + "=\"";
    auto pos = tag.find(key);
    if (pos == std::string_view::npos) return {};
    pos += key.size();
    auto end = tag.find('"', pos);
    if (end == std::string_view::npos) return {};
    return xmlUnescape(tag.substr(pos, end - pos));
}

std::string getInnerTagText(std::string_view xml, std::string_view tagName) {
    std::string open = "<" + std::string(tagName);
    auto start = xml.find(open);
    if (start == std::string_view::npos) return {};
    auto gt = xml.find('>', start);
    if (gt == std::string_view::npos) return {};
    std::string close = "</" + std::string(tagName) + ">";
    auto end = xml.find(close, gt + 1);
    if (end == std::string_view::npos) return {};
    return xmlUnescape(xml.substr(gt + 1, end - (gt + 1)));
}

ZipEntry makeTextEntry(std::string name, const std::string& content) {
    return ZipEntry{std::move(name), std::vector<std::uint8_t>(content.begin(), content.end())};
}

} // namespace

Result<std::vector<std::uint8_t>> XlsxCodec::encode(const Workbook& wb) const {
    std::vector<ZipEntry> entries;
    auto sheetIds = wb.sheetIds();

    // Shared strings table
    std::vector<std::string> sharedStrings;
    std::unordered_map<std::string, std::size_t> ssIndex;
    auto internString = [&](const std::string& s) -> std::size_t {
        auto it = ssIndex.find(s);
        if (it != ssIndex.end()) return it->second;
        std::size_t idx = sharedStrings.size();
        sharedStrings.push_back(s);
        ssIndex.emplace(s, idx);
        return idx;
    };

    // Build worksheet XMLs
    std::ostringstream ct;
    ct << "<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?>\n"
       << "<Types xmlns=\"http://schemas.openxmlformats.org/package/2006/content-types\">"
       << "<Default Extension=\"rels\" ContentType=\"application/vnd.openxmlformats-package.relationships+xml\"/>"
       << "<Default Extension=\"xml\" ContentType=\"application/xml\"/>"
       << "<Override PartName=\"/xl/workbook.xml\" ContentType=\"application/vnd.openxmlformats-officedocument.spreadsheetml.sheet.main+xml\"/>"
       << "<Override PartName=\"/xl/sharedStrings.xml\" ContentType=\"application/vnd.openxmlformats-officedocument.spreadsheetml.sharedStrings+xml\"/>";

    std::ostringstream wbXml;
    wbXml << "<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?>\n"
          << "<workbook xmlns=\"http://schemas.openxmlformats.org/spreadsheetml/2006/main\" "
          << "xmlns:r=\"http://schemas.openxmlformats.org/officeDocument/2006/relationships\"><sheets>";

    std::ostringstream wbRels;
    wbRels << "<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?>\n"
           << "<Relationships xmlns=\"http://schemas.openxmlformats.org/package/2006/relationships\">"
           << "<Relationship Id=\"rIdSS\" Type=\"http://schemas.openxmlformats.org/officeDocument/2006/relationships/sharedStrings\" Target=\"sharedStrings.xml\"/>";

    for (std::size_t i = 0; i < sheetIds.size(); ++i) {
        const Worksheet* ws = wb.getSheet(sheetIds[i]);
        std::size_t idx1 = i + 1;
        std::string partPath = "xl/worksheets/sheet" + std::to_string(idx1) + ".xml";
        ct << "<Override PartName=\"/" << partPath
           << "\" ContentType=\"application/vnd.openxmlformats-officedocument.spreadsheetml.worksheet+xml\"/>";
        wbXml << "<sheet name=\"" << xmlEscape(ws->name()) << "\" sheetId=\"" << idx1
              << "\" r:id=\"rId" << idx1 << "\"/>";
        wbRels << "<Relationship Id=\"rId" << idx1
               << "\" Type=\"http://schemas.openxmlformats.org/officeDocument/2006/relationships/worksheet\" Target=\"worksheets/sheet"
               << idx1 << ".xml\"/>";

        // Group cells by row in ascending order
        std::map<RowIndex, std::map<ColIndex, CellRecord>> rows;
        ws->forEachCell([&](CellCoord c, const CellRecord& rec) {
            rows[c.row][c.col] = rec;
        });

        std::ostringstream shXml;
        shXml << "<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?>\n"
              << "<worksheet xmlns=\"http://schemas.openxmlformats.org/spreadsheetml/2006/main\">";
        if (ws->frozenPanes().rows > 0 || ws->frozenPanes().cols > 0) {
            shXml << "<sheetViews><sheetView workbookViewId=\"0\"><pane "
                  << "xSplit=\"" << ws->frozenPanes().cols << "\" "
                  << "ySplit=\"" << ws->frozenPanes().rows << "\" "
                  << "state=\"frozen\"/></sheetView></sheetViews>";
        }
        shXml << "<sheetData>";

        for (const auto& [r, cols] : rows) {
            shXml << "<row r=\"" << (r + 1) << "\">";
            for (const auto& [c, rec] : cols) {
                std::string ref = CellCoord{r, c}.toA1();
                if (rec.formulaText.has_value()) {
                    std::string fbody = rec.formulaText->front() == '=' ? rec.formulaText->substr(1) : *rec.formulaText;
                    shXml << "<c r=\"" << ref << "\"><f>" << xmlEscape(fbody) << "</f>";
                    if (isNumber(rec.value)) {
                        shXml << "<v>" << std::get<double>(rec.value) << "</v>";
                    }
                    shXml << "</c>";
                } else if (isNumber(rec.value)) {
                    shXml << "<c r=\"" << ref << "\"><v>" << std::get<double>(rec.value) << "</v></c>";
                } else if (isText(rec.value)) {
                    std::size_t sIdx = internString(std::get<std::string>(rec.value));
                    shXml << "<c r=\"" << ref << "\" t=\"s\"><v>" << sIdx << "</v></c>";
                } else if (isBool(rec.value)) {
                    shXml << "<c r=\"" << ref << "\" t=\"b\"><v>" << (std::get<bool>(rec.value) ? "1" : "0") << "</v></c>";
                } else if (isDateTime(rec.value)) {
                    shXml << "<c r=\"" << ref << "\"><v>" << std::get<DateTimeValue>(rec.value).serial << "</v></c>";
                }
            }
            shXml << "</row>";
        }
        shXml << "</sheetData>";
        if (!ws->mergedRegions().empty()) {
            shXml << "<mergeCells count=\"" << ws->mergedRegions().size() << "\">";
            for (const auto& mr : ws->mergedRegions()) {
                shXml << "<mergeCell ref=\"" << mr.toA1() << "\"/>";
            }
            shXml << "</mergeCells>";
        }
        shXml << "</worksheet>";
        entries.push_back(makeTextEntry(partPath, shXml.str()));
    }

    ct << "</Types>";
    wbXml << "</sheets>";
    if (!wb.namedRanges().all().empty()) {
        wbXml << "<definedNames>";
        for (const auto& [_, nr] : wb.namedRanges().all()) {
            wbXml << "<definedName name=\"" << xmlEscape(nr.name) << "\">"
                  << xmlEscape(nr.expression) << "</definedName>";
        }
        wbXml << "</definedNames>";
    }
    wbXml << "</workbook>";
    wbRels << "</Relationships>";

    std::ostringstream ssXml;
    ssXml << "<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?>\n"
          << "<sst xmlns=\"http://schemas.openxmlformats.org/spreadsheetml/2006/main\" count=\""
          << sharedStrings.size() << "\" uniqueCount=\"" << sharedStrings.size() << "\">";
    for (const auto& s : sharedStrings) {
        ssXml << "<si><t>" << xmlEscape(s) << "</t></si>";
    }
    ssXml << "</sst>";

    std::string rootRels =
        "<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?>\n"
        "<Relationships xmlns=\"http://schemas.openxmlformats.org/package/2006/relationships\">"
        "<Relationship Id=\"rId1\" Type=\"http://schemas.openxmlformats.org/officeDocument/2006/relationships/officeDocument\" Target=\"xl/workbook.xml\"/>"
        "</Relationships>";

    entries.push_back(makeTextEntry("[Content_Types].xml", ct.str()));
    entries.push_back(makeTextEntry("_rels/.rels", rootRels));
    entries.push_back(makeTextEntry("xl/workbook.xml", wbXml.str()));
    entries.push_back(makeTextEntry("xl/_rels/workbook.xml.rels", wbRels.str()));
    entries.push_back(makeTextEntry("xl/sharedStrings.xml", ssXml.str()));

    return writeZipStore(entries);
}

Result<void> XlsxCodec::decode(const std::vector<std::uint8_t>& bytes, Workbook& wb) const {
    auto filesRes = readZipStore(bytes);
    if (!filesRes) return filesRes.error();
    const auto& files = filesRes.value();

    // Parse shared strings if present
    std::vector<std::string> sharedStrings;
    if (auto it = files.find("xl/sharedStrings.xml"); it != files.end()) {
        std::string_view xml = it->second;
        std::size_t pos = 0;
        while (true) {
            auto siStart = xml.find("<si>", pos);
            if (siStart == std::string_view::npos) break;
            auto siEnd = xml.find("</si>", siStart);
            if (siEnd == std::string_view::npos) break;
            sharedStrings.push_back(getInnerTagText(xml.substr(siStart, siEnd - siStart), "t"));
            pos = siEnd + 5;
        }
    }

    auto wbIt = files.find("xl/workbook.xml");
    if (wbIt == files.end()) {
        return Error{ErrorCode::IOError, "Missing xl/workbook.xml in XLSX"};
    }

    // Parse <sheet name="..." .../>
    std::string_view wbXml = wbIt->second;
    std::size_t pos = 0;
    std::size_t sheetIdx = 0;
    while (true) {
        auto shStart = wbXml.find("<sheet ", pos);
        if (shStart == std::string_view::npos) break;
        auto shEnd = wbXml.find("/>", shStart);
        if (shEnd == std::string_view::npos) break;
        std::string_view tag = wbXml.substr(shStart, shEnd - shStart);
        std::string name = getAttr(tag, "name");
        ++sheetIdx;

        auto wsRes = wb.addSheet(name.empty() ? ("Sheet" + std::to_string(sheetIdx)) : name);
        if (!wsRes) return wsRes.error();
        Worksheet* ws = wsRes.value();

        std::string partPath = "xl/worksheets/sheet" + std::to_string(sheetIdx) + ".xml";
        if (auto sit = files.find(partPath); sit != files.end()) {
            std::string_view sxml = sit->second;
            std::size_t cpos = 0;
            while (true) {
                auto cStart = sxml.find("<c ", cpos);
                if (cStart == std::string_view::npos) break;
                auto cTagEnd = sxml.find('>', cStart);
                if (cTagEnd == std::string_view::npos) break;
                bool selfClosing = (sxml[cTagEnd - 1] == '/');
                std::size_t cClose = selfClosing ? cTagEnd + 1 : sxml.find("</c>", cTagEnd);
                if (!selfClosing && cClose == std::string_view::npos) break;

                std::string_view cHeader = sxml.substr(cStart, cTagEnd - cStart);
                std::string ref = getAttr(cHeader, "r");
                std::string typeAttr = getAttr(cHeader, "t");
                auto coord = CellCoord::fromA1(ref);

                if (coord && !selfClosing) {
                    std::string_view body = sxml.substr(cTagEnd + 1, cClose - (cTagEnd + 1));
                    std::string fText = getInnerTagText(body, "f");
                    std::string vText = getInnerTagText(body, "v");

                    if (!fText.empty()) {
                        ws->setFormula(*coord, "=" + fText);
                        if (!vText.empty()) {
                            double d = 0.0;
                            auto [ptr, ec] = std::from_chars(vText.data(), vText.data() + vText.size(), d);
                            if (ec == std::errc{} && ptr == vText.data() + vText.size()) {
                                ws->setCachedValue(*coord, d);
                            }
                        }
                    } else if (typeAttr == "s" && !vText.empty()) {
                        std::size_t idx = 0;
                        auto [ptr, ec] = std::from_chars(vText.data(), vText.data() + vText.size(), idx);
                        if (ec == std::errc{} && idx < sharedStrings.size()) {
                            ws->setValue(*coord, sharedStrings[idx]);
                        }
                    } else if (typeAttr == "b" && !vText.empty()) {
                        ws->setValue(*coord, vText == "1");
                    } else if (!vText.empty()) {
                        double d = 0.0;
                        auto [ptr, ec] = std::from_chars(vText.data(), vText.data() + vText.size(), d);
                        if (ec == std::errc{} && ptr == vText.data() + vText.size()) {
                            ws->setValue(*coord, d);
                        } else {
                            ws->setValue(*coord, vText);
                        }
                    }
                }
                cpos = selfClosing ? (cTagEnd + 1) : (cClose + 4);
            }

            // Parse frozen pane
            if (auto pStart = sxml.find("<pane "); pStart != std::string_view::npos) {
                if (auto pEnd = sxml.find("/>", pStart); pEnd != std::string_view::npos) {
                    std::string_view ptag = sxml.substr(pStart, pEnd - pStart);
                    std::string xs = getAttr(ptag, "xSplit");
                    std::string ys = getAttr(ptag, "ySplit");
                    FrozenPanes fp{};
                    if (!xs.empty()) std::from_chars(xs.data(), xs.data() + xs.size(), fp.cols);
                    if (!ys.empty()) std::from_chars(ys.data(), ys.data() + ys.size(), fp.rows);
                    ws->setFrozenPanes(fp);
                }
            }

            // Parse <mergeCell ref="..."/>
            std::size_t mpos = 0;
            while (true) {
                auto mStart = sxml.find("<mergeCell ", mpos);
                if (mStart == std::string_view::npos) break;
                auto mEnd = sxml.find("/>", mStart);
                if (mEnd == std::string_view::npos) break;
                std::string mref = getAttr(sxml.substr(mStart, mEnd - mStart), "ref");
                if (auto rng = RangeAddress::fromA1(mref)) {
                    ws->mergeCells(*rng);
                }
                mpos = mEnd + 2;
            }
        }
        pos = shEnd + 2;
    }

    // Parse <definedName name="...">...</definedName>
    std::size_t dpos = 0;
    while (true) {
        auto dStart = wbXml.find("<definedName ", dpos);
        if (dStart == std::string_view::npos) break;
        auto dTagEnd = wbXml.find('>', dStart);
        if (dTagEnd == std::string_view::npos) break;
        auto dClose = wbXml.find("</definedName>", dTagEnd);
        if (dClose == std::string_view::npos) break;
        std::string dname = getAttr(wbXml.substr(dStart, dTagEnd - dStart), "name");
        std::string dexpr = xmlUnescape(wbXml.substr(dTagEnd + 1, dClose - (dTagEnd + 1)));
        if (!dname.empty()) {
            wb.namedRanges().define(NamedRangeDef{std::move(dname), kInvalidSheetId, std::move(dexpr)});
        }
        dpos = dClose + 14;
    }

    wb.markSaved();
    return Result<void>::ok();
}

} // namespace sheetcore::io
