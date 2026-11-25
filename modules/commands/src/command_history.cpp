#include "sheetcore/commands/command.hpp"

namespace sheetcore::commands {

CompositeCommand::CompositeCommand(std::string desc) : description_(std::move(desc)) {}

void CompositeCommand::add(std::unique_ptr<ICommand> cmd) {
    if (cmd) {
        commands_.push_back(std::move(cmd));
    }
}

Result<void> CompositeCommand::execute(Workbook& wb) {
    for (std::size_t i = 0; i < commands_.size(); ++i) {
        auto res = commands_[i]->execute(wb);
        if (!res) {
            // Rollback already-executed subcommands in reverse order
            for (std::size_t j = i; j > 0; --j) {
                commands_[j - 1]->undo(wb);
            }
            return res;
        }
    }
    return Result<void>::ok();
}

Result<void> CompositeCommand::undo(Workbook& wb) {
    for (std::size_t i = commands_.size(); i > 0; --i) {
        auto res = commands_[i - 1]->undo(wb);
        if (!res) {
            return res;
        }
    }
    return Result<void>::ok();
}

Result<void> CommandHistory::execute(std::unique_ptr<ICommand> cmd) {
    if (!cmd) {
        return Error{ErrorCode::InvalidArgument, "Null command"};
    }
    auto res = cmd->execute(workbook_);
    if (!res) {
        return res;
    }
    if (!txStack_.empty()) {
        txStack_.back()->add(std::move(cmd));
    } else {
        undoStack_.push_back(std::move(cmd));
        redoStack_.clear();
    }
    return Result<void>::ok();
}

Result<void> CommandHistory::undo() {
    if (undoStack_.empty()) {
        return Error{ErrorCode::NotFound, "Nothing to undo"};
    }
    auto cmd = std::move(undoStack_.back());
    undoStack_.pop_back();
    auto res = cmd->undo(workbook_);
    if (!res) {
        undoStack_.push_back(std::move(cmd));
        return res;
    }
    redoStack_.push_back(std::move(cmd));
    return Result<void>::ok();
}

Result<void> CommandHistory::redo() {
    if (redoStack_.empty()) {
        return Error{ErrorCode::NotFound, "Nothing to redo"};
    }
    auto cmd = std::move(redoStack_.back());
    redoStack_.pop_back();
    auto res = cmd->execute(workbook_);
    if (!res) {
        redoStack_.push_back(std::move(cmd));
        return res;
    }
    undoStack_.push_back(std::move(cmd));
    return Result<void>::ok();
}

void CommandHistory::beginTransaction(std::string description) {
    txStack_.push_back(std::make_unique<CompositeCommand>(std::move(description)));
}

Result<void> CommandHistory::commitTransaction() {
    if (txStack_.empty()) {
        return Error{ErrorCode::InvalidArgument, "No active transaction"};
    }
    auto tx = std::move(txStack_.back());
    txStack_.pop_back();
    if (!tx->empty()) {
        if (!txStack_.empty()) {
            txStack_.back()->add(std::move(tx));
        } else {
            undoStack_.push_back(std::move(tx));
            redoStack_.clear();
        }
    }
    return Result<void>::ok();
}

Result<void> CommandHistory::rollbackTransaction() {
    if (txStack_.empty()) {
        return Error{ErrorCode::InvalidArgument, "No active transaction"};
    }
    auto tx = std::move(txStack_.back());
    txStack_.pop_back();
    return tx->undo(workbook_);
}

void CommandHistory::clear() {
    undoStack_.clear();
    redoStack_.clear();
    txStack_.clear();
}

} // namespace sheetcore::commands
