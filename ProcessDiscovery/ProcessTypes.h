#pragma once

#include <string>
#include <optional>
#include <cstdint>
#include <vector>
#include <utility>
#include <cstddef>
#include <functional>

enum class FieldState {
    available,
    unavailable,
    not_requested,
    access_denied,
    process_exited,
    unsupported_architecture,
    available_unverified,
    error
};

enum class ErrorDomain {
    none,
    win32,
    wintrust,
    version_resource,
    validation,
    cancellation,
    internal
};

struct ErrorInfo {
    std::string operation;
    std::uint32_t native_code{}; // código nativo bruto; operation identifica a origem
    std::string message;
    ErrorDomain domain = ErrorDomain::none;
};

template<class T>
class Field {
public:
    constexpr Field() = default;

    static Field available(T value) {
        return Field(FieldState::available, std::move(value), std::nullopt);
    }

    static Field unavailable(std::optional<ErrorInfo> diagnostic = std::nullopt) {
        return Field(FieldState::unavailable, std::nullopt, std::move(diagnostic));
    }

    static Field available_unverified(T value, std::optional<ErrorInfo> diagnostic = std::nullopt) {
        return Field(FieldState::available_unverified, std::move(value), std::move(diagnostic));
    }

    static Field not_requested() {
        return Field();
    }

    static Field access_denied(std::optional<ErrorInfo> diagnostic = std::nullopt) {
        return Field(FieldState::access_denied, std::nullopt, std::move(diagnostic));
    }

    static Field process_exited(std::optional<ErrorInfo> diagnostic = std::nullopt) {
        return Field(FieldState::process_exited, std::nullopt, std::move(diagnostic));
    }

    static Field unsupported_architecture(std::optional<ErrorInfo> diagnostic = std::nullopt) {
        return Field(FieldState::unsupported_architecture, std::nullopt, std::move(diagnostic));
    }

    static Field failure(ErrorInfo error) {
        return Field(FieldState::error, std::nullopt, std::move(error));
    }

    constexpr FieldState state() const noexcept { return state_; }
    const std::optional<T>& value() const noexcept { return value_; }
    const std::optional<ErrorInfo>& error() const noexcept { return error_; }

private:
    Field(FieldState state, std::optional<T> value, std::optional<ErrorInfo> error)
        : state_(state), value_(std::move(value)), error_(std::move(error)) {}

    FieldState state_{FieldState::not_requested};
    std::optional<T> value_;
    std::optional<ErrorInfo> error_;
};

enum class ObservationConsistency {
    snapshot_only,
    handle_only,
    best_effort_unverified,
    mismatch_detected
};

enum class ResultState { complete, partial, failed };

template<class T>
struct CollectionResult {
    std::vector<T> items;
    ResultState state = ResultState::complete;
    std::vector<ErrorInfo> diagnostics;
};

enum class SignaturePresence { present, no_embedded_signature, unknown };
enum class TrustState { valid, invalid, indeterminate, not_evaluated };

struct SignatureStatus {
    SignaturePresence presence;
    TrustState trust;
    std::int32_t wintrust_status; // LONG de WinVerifyTrust; sucesso somente zero
    std::optional<std::uint32_t> last_error;
    std::wstring policy;
};

struct ModuleRecord {
    Field<std::wstring> name;
    Field<std::wstring> path;
    std::uint64_t base_address = 0;
};

struct ProcessRecord {
    std::uint32_t pid = -1;
    Field<std::uint32_t> ppid;
    Field<std::wstring> name;
	Field<std::wstring> image_path;
    Field<std::wstring> parent_name;
	Field<std::uint64_t> creation_time_100ns;
    Field<std::wstring> internal_name;
    Field<CollectionResult<ModuleRecord>> modules;
    Field<SignatureStatus> signature;
    ObservationConsistency observation_consistency = ObservationConsistency::snapshot_only;
};

struct QueryResult {
    std::vector<ProcessRecord> processes;
    ResultState state = ResultState::complete;
    std::optional<ErrorInfo> fatal_error;
    std::vector<ErrorInfo> diagnostics;
    std::size_t unevaluable_processes = 0;
};

class StopToken {
public:
    using Predicate = std::function<bool()>;

    StopToken() = default;
    explicit StopToken(Predicate predicate) : predicate_(std::move(predicate)) {}
    bool stop_requested() const { return predicate_ != nullptr && predicate_(); }

private:
    Predicate predicate_;
};

struct InspectionOptions {
    bool include_image_path = true;
    bool include_internal_name = false;
    bool include_signature = false;
    bool include_modules = false;
    bool allow_remote_file_inspection = false;
    const StopToken* stop_token = nullptr;
};