#include "sheetcore/storage/storage.hpp"

#include <cctype>
#include <fstream>
#include <system_error>

namespace sheetcore::storage {

std::string StorageManager::normalizeExt(std::string_view ext) {
    std::string out;
    out.reserve(ext.size());
    for (char ch : ext) {
        out.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(ch))));
    }
    return out;
}

void StorageManager::registerCodec(std::shared_ptr<IWorkbookCodec> codec) {
    if (codec) {
        codecs_[normalizeExt(codec->extension())] = std::move(codec);
    }
}

const IWorkbookCodec* StorageManager::findCodec(std::string_view extension) const {
    auto it = codecs_.find(normalizeExt(extension));
    return it == codecs_.end() ? nullptr : it->second.get();
}

Result<void> StorageManager::writeAtomic(const std::filesystem::path& path,
                                         const std::vector<std::uint8_t>& bytes) {
    std::filesystem::path tmpPath = path;
    tmpPath += ".tmp";

    {
        std::ofstream out(tmpPath, std::ios::binary | std::ios::trunc);
        if (!out) {
            return Error{ErrorCode::IOError, "Failed to open temporary file for writing: " + tmpPath.string()};
        }
        if (!bytes.empty()) {
            out.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
        }
        out.flush();
        if (!out) {
            std::error_code ec;
            std::filesystem::remove(tmpPath, ec);
            return Error{ErrorCode::IOError, "Failed while writing file: " + tmpPath.string()};
        }
    }

    std::error_code ec;
    std::filesystem::rename(tmpPath, path, ec);
    if (ec) {
        std::filesystem::remove(tmpPath, ec);
        return Error{ErrorCode::IOError, "Failed to finalize atomic save: " + ec.message()};
    }
    return Result<void>::ok();
}

Result<std::vector<std::uint8_t>> StorageManager::readAllBytes(const std::filesystem::path& path) {
    std::ifstream in(path, std::ios::binary | std::ios::ate);
    if (!in) {
        return Error{ErrorCode::IOError, "Failed to open file: " + path.string()};
    }
    auto size = in.tellg();
    if (size < 0) {
        return Error{ErrorCode::IOError, "Failed to determine file size: " + path.string()};
    }
    std::vector<std::uint8_t> bytes(static_cast<std::size_t>(size));
    in.seekg(0, std::ios::beg);
    if (!bytes.empty()) {
        in.read(reinterpret_cast<char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
        if (!in) {
            return Error{ErrorCode::IOError, "Failed to read file: " + path.string()};
        }
    }
    return bytes;
}

Result<void> StorageManager::save(const Workbook& wb, const std::filesystem::path& path) const {
    const IWorkbookCodec* codec = findCodec(path.extension().string());
    if (!codec) {
        return Error{ErrorCode::Unsupported, "No codec registered for extension: " + path.extension().string()};
    }
    auto bytes = codec->encode(wb);
    if (!bytes) return bytes.error();
    return writeAtomic(path, bytes.value());
}

Result<void> StorageManager::load(Workbook& wb, const std::filesystem::path& path) const {
    const IWorkbookCodec* codec = findCodec(path.extension().string());
    if (!codec) {
        return Error{ErrorCode::Unsupported, "No codec registered for extension: " + path.extension().string()};
    }
    auto bytes = readAllBytes(path);
    if (!bytes) return bytes.error();
    return codec->decode(bytes.value(), wb);
}

} // namespace sheetcore::storage
