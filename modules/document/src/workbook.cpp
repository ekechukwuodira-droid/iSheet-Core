#include "sheetcore/document/workbook.hpp"

#include <algorithm>

namespace sheetcore {

Workbook Workbook::create() {
    return Workbook{};
}

Workbook::Workbook(Workbook&& other) noexcept
    : nextSheetId_(other.nextSheetId_),
      sheets_(std::move(other.sheets_)),
      calcMode_(other.calcMode_),
      modified_(other.modified_),
      namedRanges_(std::move(other.namedRanges_)),
      observers_(std::move(other.observers_)) {
    for (auto& s : sheets_) {
        s->rebindOwner(*this);
    }
}

Workbook& Workbook::operator=(Workbook&& other) noexcept {
    if (this != &other) {
        nextSheetId_ = other.nextSheetId_;
        sheets_ = std::move(other.sheets_);
        calcMode_ = other.calcMode_;
        modified_ = other.modified_;
        namedRanges_ = std::move(other.namedRanges_);
        observers_ = std::move(other.observers_);
        for (auto& s : sheets_) {
            s->rebindOwner(*this);
        }
    }
    return *this;
}

Result<Worksheet*> Workbook::addSheet(std::string name) {
    if (name.empty()) {
        return Error{ErrorCode::InvalidArgument, "Sheet name cannot be empty"};
    }
    if (getSheet(name) != nullptr) {
        return Error{ErrorCode::AlreadyExists, "Sheet already exists: " + name};
    }
    SheetId id = nextSheetId_++;
    sheets_.push_back(std::make_unique<Worksheet>(*this, id, std::move(name)));
    Worksheet* ptr = sheets_.back().get();
    modified_ = true;
    notify(WorkbookEvent{WorkbookEventType::SheetAdded, id, std::nullopt, ptr->name()});
    return ptr;
}

bool Workbook::removeSheet(SheetId id) {
    auto it = std::find_if(sheets_.begin(), sheets_.end(),
        [id](const auto& s) { return s->id() == id; });
    if (it == sheets_.end()) {
        return false;
    }
    std::string removedName = (*it)->name();
    sheets_.erase(it);
    modified_ = true;
    notify(WorkbookEvent{WorkbookEventType::SheetRemoved, id, std::nullopt, std::move(removedName)});
    return true;
}

bool Workbook::renameSheet(SheetId id, std::string newName) {
    if (newName.empty()) {
        return false;
    }
    if (const auto* existing = getSheet(newName); existing != nullptr && existing->id() != id) {
        return false;
    }
    Worksheet* sheet = getSheet(id);
    if (!sheet) {
        return false;
    }
    sheet->setNameInternal(std::move(newName));
    modified_ = true;
    notify(WorkbookEvent{WorkbookEventType::SheetRenamed, id, std::nullopt, sheet->name()});
    return true;
}

Worksheet* Workbook::getSheet(SheetId id) noexcept {
    for (auto& s : sheets_) {
        if (s->id() == id) {
            return s.get();
        }
    }
    return nullptr;
}

const Worksheet* Workbook::getSheet(SheetId id) const noexcept {
    for (const auto& s : sheets_) {
        if (s->id() == id) {
            return s.get();
        }
    }
    return nullptr;
}

Worksheet* Workbook::getSheet(std::string_view name) noexcept {
    for (auto& s : sheets_) {
        if (s->name() == name) {
            return s.get();
        }
    }
    return nullptr;
}

const Worksheet* Workbook::getSheet(std::string_view name) const noexcept {
    for (const auto& s : sheets_) {
        if (s->name() == name) {
            return s.get();
        }
    }
    return nullptr;
}

std::vector<SheetId> Workbook::sheetIds() const {
    std::vector<SheetId> ids;
    ids.reserve(sheets_.size());
    for (const auto& s : sheets_) {
        ids.push_back(s->id());
    }
    return ids;
}

void Workbook::addObserver(IWorkbookObserver* observer) {
    if (observer && std::find(observers_.begin(), observers_.end(), observer) == observers_.end()) {
        observers_.push_back(observer);
    }
}

void Workbook::removeObserver(IWorkbookObserver* observer) {
    observers_.erase(std::remove(observers_.begin(), observers_.end(), observer), observers_.end());
}

void Workbook::notify(const WorkbookEvent& event) {
    for (IWorkbookObserver* obs : observers_) {
        if (obs) {
            obs->onWorkbookEvent(event);
        }
    }
}

} // namespace sheetcore
