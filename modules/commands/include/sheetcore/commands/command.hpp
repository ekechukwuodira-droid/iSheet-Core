#pragma once

#include "sheetcore/core/result.hpp"
#include "sheetcore/document/workbook.hpp"

#include <memory>
#include <string>
#include <vector>

namespace sheetcore::commands {

class ICommand {
public:
    virtual ~ICommand() = default;
    virtual std::string description() const = 0;
    virtual Result<void> execute(Workbook& wb) = 0;
    virtual Result<void> undo(Workbook& wb) = 0;
};

class CompositeCommand final : public ICommand {
public:
    explicit CompositeCommand(std::string desc);

    void add(std::unique_ptr<ICommand> cmd);
    bool empty() const noexcept { return commands_.empty(); }
    std::size_t size() const noexcept { return commands_.size(); }

    std::string description() const override { return description_; }
    Result<void> execute(Workbook& wb) override;
    Result<void> undo(Workbook& wb) override;

private:
    std::string description_;
    std::vector<std::unique_ptr<ICommand>> commands_;
};

class CommandHistory {
public:
    explicit CommandHistory(Workbook& workbook) : workbook_(workbook) {}

    Result<void> execute(std::unique_ptr<ICommand> cmd);

    bool canUndo() const noexcept { return !undoStack_.empty(); }
    bool canRedo() const noexcept { return !redoStack_.empty(); }

    Result<void> undo();
    Result<void> redo();

    void beginTransaction(std::string description = "Transaction");
    Result<void> commitTransaction();
    Result<void> rollbackTransaction();
    bool inTransaction() const noexcept { return !txStack_.empty(); }

    void clear();

private:
    Workbook& workbook_;
    std::vector<std::unique_ptr<ICommand>> undoStack_;
    std::vector<std::unique_ptr<ICommand>> redoStack_;
    std::vector<std::unique_ptr<CompositeCommand>> txStack_;
};

} // namespace sheetcore::commands
