#pragma once

#include <string>
#include <type_traits>
#include <utility>
#include <variant>

namespace sheetcore {

enum class ErrorCode {
    Ok = 0,
    InvalidArgument,
    NotFound,
    AlreadyExists,
    OutOfRange,
    ParseError,
    CircularReference,
    ValidationFailed,
    IOError,
    Unsupported,
    InternalError
};

struct Error {
    ErrorCode code = ErrorCode::InternalError;
    std::string message;
};

template <typename T, typename E = Error>
class Result {
public:
    Result(const T& value) : storage_(value) {}
    Result(T&& value) : storage_(std::move(value)) {}
    template <typename U, typename = std::enable_if_t<std::is_convertible_v<U, T> && !std::is_same_v<std::decay_t<U>, Result>>>
    Result(U&& value) : storage_(T(std::forward<U>(value))) {}
    Result(const E& err) : storage_(err) {}
    Result(E&& err) : storage_(std::move(err)) {}

    bool isOk() const noexcept { return std::holds_alternative<T>(storage_); }
    bool isErr() const noexcept { return std::holds_alternative<E>(storage_); }
    explicit operator bool() const noexcept { return isOk(); }

    const T& value() const& { return std::get<T>(storage_); }
    T& value() & { return std::get<T>(storage_); }
    T&& value() && { return std::get<T>(std::move(storage_)); }

    const T& operator*() const& { return value(); }
    T& operator*() & { return value(); }
    T&& operator*() && { return std::move(value()); }
    const T* operator->() const { return &value(); }
    T* operator->() { return &value(); }

    const E& error() const& { return std::get<E>(storage_); }
    E& error() & { return std::get<E>(storage_); }

    template <typename U>
    T valueOr(U&& fallback) const& {
        return isOk() ? std::get<T>(storage_) : static_cast<T>(std::forward<U>(fallback));
    }

private:
    std::variant<T, E> storage_;
};

template <typename E>
class Result<void, E> {
public:
    Result() : error_() {}
    Result(const E& err) : error_(err), hasError_(true) {}
    Result(E&& err) : error_(std::move(err)), hasError_(true) {}

    static Result ok() { return Result(); }

    bool isOk() const noexcept { return !hasError_; }
    bool isErr() const noexcept { return hasError_; }
    explicit operator bool() const noexcept { return isOk(); }

    const E& error() const& { return error_; }

private:
    E error_{};
    bool hasError_ = false;
};

} // namespace sheetcore
