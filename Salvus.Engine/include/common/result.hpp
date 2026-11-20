#pragma once

#include <optional>
#include <string>
#include <utility>

namespace salvus::common {

enum class StatusCode {
    Ok = 0,
    NotImplemented,
    InvalidArgument,
    OutOfBounds,
    AccessDenied,
    IoError,
    ProtocolError,
    InternalError
};

struct Error {
    StatusCode code = StatusCode::InternalError;
    std::string message;
};

template <typename T>
class Result {
public:
    static Result Ok(T value) {
        return Result(std::move(value));
    }

    static Result Fail(StatusCode code, std::string message) {
        return Result(Error{code, std::move(message)});
    }

    [[nodiscard]] bool ok() const noexcept {
        return value_.has_value();
    }

    [[nodiscard]] const T& value() const {
        return *value_;
    }

    [[nodiscard]] T& value() {
        return *value_;
    }

    [[nodiscard]] const Error& error() const {
        return *error_;
    }

private:
    explicit Result(T value) : value_(std::move(value)) {}
    explicit Result(Error error) : error_(std::move(error)) {}

    std::optional<T> value_;
    std::optional<Error> error_;
};

template <>
class Result<void> {
public:
    static Result Ok() {
        return Result();
    }

    static Result Fail(StatusCode code, std::string message) {
        return Result(Error{code, std::move(message)});
    }

    [[nodiscard]] bool ok() const noexcept {
        return !error_.has_value();
    }

    [[nodiscard]] const Error& error() const {
        return *error_;
    }

private:
    Result() = default;
    explicit Result(Error error) : error_(std::move(error)) {}

    std::optional<Error> error_;
};

} // namespace salvus::common
