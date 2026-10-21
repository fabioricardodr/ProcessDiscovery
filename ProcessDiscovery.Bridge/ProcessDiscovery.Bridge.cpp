#include "include/process_discovery_bridge.h"
#include "BridgeLoader.h"
#include "../ProcessDiscovery/ProcessCatalog.h"
#include "../ProcessDiscovery/ProcessTypes.h"
#include "../third_party/nlohmann/json.hpp"

#include <Windows.h>
#include <atomic>
#include <chrono>
#include <codecvt>
#include <cstring>
#include <condition_variable>
#include <exception>
#include <memory>
#include <mutex>
#include <new>
#include <string>
#include <utility>

using json = nlohmann::json;

struct pd_cancellation_handle_t {
	std::atomic_bool requested{false};
	std::atomic_uint32_t active{0};
	std::mutex mutex;
};

struct pd_result_handle_t {
	std::string json_utf8;
};

struct pd_catalog_handle_t {
	std::unique_ptr<ProcessCatalog> native;
	std::mutex mutex;
	std::atomic_uint32_t active{0};
	std::atomic_bool freed{false};
	std::string last_operation;
	uint32_t last_code{};
	std::string last_message;
	std::atomic_bool fail_next_json{false};
#if defined(PD_TESTING)
	std::mutex testing_mutex;
	std::condition_variable testing_condition;
	bool testing_block_query{};
	bool testing_query_blocked{};
	bool testing_release_query{};
#endif
};

namespace {

thread_local uint64_t diagnostic_sequence = 0;

std::string utf8(const std::wstring& value) {
	if (value.empty()) return {};
	const int size = WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, value.data(), static_cast<int>(value.size()), nullptr, 0, nullptr, nullptr);
	if (size <= 0) return {};
	std::string result(static_cast<size_t>(size), '\0');
	WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, value.data(), static_cast<int>(value.size()), result.data(), size, nullptr, nullptr);
	return result;
}

const char* domain(ErrorDomain domain) {
	switch (domain) {
	case ErrorDomain::none: return "none";
	case ErrorDomain::win32: return "win32";
	case ErrorDomain::wintrust: return "wintrust";
	case ErrorDomain::version_resource: return "version_resource";
	case ErrorDomain::validation: return "validation";
	case ErrorDomain::cancellation: return "cancellation";
	case ErrorDomain::internal: return "internal";
	}
	return "internal";
}

const char* field_state(FieldState state) {
	switch (state) {
	case FieldState::available: return "available";
	case FieldState::unavailable: return "unavailable";
	case FieldState::not_requested: return "not_requested";
	case FieldState::access_denied: return "access_denied";
	case FieldState::process_exited: return "process_exited";
	case FieldState::unsupported_architecture: return "unsupported_architecture";
	case FieldState::available_unverified: return "available_unverified";
	case FieldState::error: return "error";
	}
	return "error";
}

const char* result_state(ResultState state) {
	switch (state) {
	case ResultState::complete: return "complete";
	case ResultState::partial: return "partial";
	case ResultState::failed: return "failed";
	}
	return "failed";
}

const char* consistency(ObservationConsistency value) {
	switch (value) {
	case ObservationConsistency::snapshot_only: return "snapshot_only";
	case ObservationConsistency::handle_only: return "handle_only";
	case ObservationConsistency::best_effort_unverified: return "best_effort_unverified";
	case ObservationConsistency::mismatch_detected: return "mismatch_detected";
	}
	return "snapshot_only";
}

const char* signature_presence(SignaturePresence value) {
	switch (value) {
	case SignaturePresence::present: return "present";
	case SignaturePresence::no_embedded_signature: return "no_embedded_signature";
	case SignaturePresence::unknown: return "unknown";
	}
	return "unknown";
}

const char* trust_state(TrustState value) {
	switch (value) {
	case TrustState::valid: return "valid";
	case TrustState::invalid: return "invalid";
	case TrustState::indeterminate: return "indeterminate";
	case TrustState::not_evaluated: return "not_evaluated";
	}
	return "not_evaluated";
}

json error_json(const ErrorInfo& error, const char* origin, const char* scope, uint32_t pid = 0, const char* field = nullptr, const char* collection = nullptr, const char* kind = "other") {
	const std::string diagnostic_id = std::string(origin) + ":" + scope + ":" + error.operation + ":" +
		std::to_string(pid) + ":" + std::to_string(error.native_code) + ":" + error.message + ":" +
		std::to_string(++diagnostic_sequence);
	json result = {
		{"diagnostic_id", diagnostic_id},
		{"diagnostic_kind", kind},
		{"origin", origin},
		{"operation", error.operation},
		{"native_code", error.native_code},
		{"message", error.message},
		{"domain", domain(error.domain)},
		{"scope", scope}
	};
	if (std::string(scope) != "global") result["process_pid"] = pid;
	if (field != nullptr) result["field"] = field;
	if (collection != nullptr) result["collection"] = collection;
	return result;
}

json optional_error(const std::optional<ErrorInfo>& error, const char* origin, const char* scope, uint32_t pid, const char* field) {
	return error.has_value() ? error_json(*error, origin, scope, pid, field) : json(nullptr);
}

template<class T, class Convert>
json field_json(const Field<T>& field, Convert convert, uint32_t pid, const char* name) {
	json result = {{"state", field_state(field.state())}, {"value", nullptr}, {"diagnostic", nullptr}};
	if (field.value().has_value()) result["value"] = convert(*field.value());
	if (field.error().has_value()) result["diagnostic"] = error_json(*field.error(), "field", "field", pid, name);
	return result;
}

json module_json(const ModuleRecord& module, uint32_t pid) {
	return {
		{"name", field_json(module.name, [](const std::wstring& value) { return json(utf8(value)); }, pid, "name")},
		{"path", field_json(module.path, [](const std::wstring& value) { return json(utf8(value)); }, pid, "path")},
		{"base_address", module.base_address}
	};
}

json process_json(const ProcessRecord& process) {
	const uint32_t pid = process.pid;
	json result = {
		{"pid", pid},
		{"ppid", field_json(process.ppid, [](uint32_t value) { return json(value); }, pid, "ppid")},
		{"name", field_json(process.name, [](const std::wstring& value) { return json(utf8(value)); }, pid, "name")},
		{"image_path", field_json(process.image_path, [](const std::wstring& value) { return json(utf8(value)); }, pid, "image_path")},
		{"parent_name", field_json(process.parent_name, [](const std::wstring& value) { return json(utf8(value)); }, pid, "parent_name")},
		{"creation_time_100ns", field_json(process.creation_time_100ns, [](uint64_t value) { return json(value); }, pid, "creation_time_100ns")},
		{"internal_name", field_json(process.internal_name, [](const std::wstring& value) { return json(utf8(value)); }, pid, "internal_name")},
		{"observation_consistency", consistency(process.observation_consistency)},
		{"diagnostics", json::array()}
	};

	json modules = {{"state", "complete"}, {"items", json::array()}, {"diagnostics", json::array()}};
	if (process.modules.value().has_value()) {
		const auto& collection = *process.modules.value();
		modules["state"] = result_state(collection.state);
		for (const auto& module : collection.items) modules["items"].push_back(module_json(module, pid));
		for (const auto& diagnostic : collection.diagnostics) modules["diagnostics"].push_back(error_json(diagnostic, "collection", "collection", pid, nullptr, "modules"));
	}
	result["modules"] = field_json(process.modules, [modules](const CollectionResult<ModuleRecord>&) { return modules; }, pid, "modules");

	json signature = { {"presence", "unknown"}, {"trust", "not_evaluated"}, {"wintrust_status", 0}, {"last_error", nullptr}, {"policy", ""} };
	if (process.signature.value().has_value()) {
		const auto& value = *process.signature.value();
		signature["presence"] = signature_presence(value.presence);
		signature["trust"] = trust_state(value.trust);
		signature["wintrust_status"] = value.wintrust_status;
		if (value.last_error.has_value()) signature["last_error"] = *value.last_error;
		signature["policy"] = utf8(value.policy);
	}
	result["signature"] = field_json(process.signature, [signature](const SignatureStatus&) { return signature; }, pid, "signature");
	for (const auto& diagnostic : process.diagnostics) result["diagnostics"].push_back(error_json(diagnostic, "process_record", "process", pid, nullptr, nullptr, "other"));
	return result;
}

json query_json(const QueryResult& query) {
	diagnostic_sequence = 0;
	json result = {
		{"schema_version", PD_SCHEMA_VERSION_3},
		{"processes", json::array()},
		{"state", result_state(query.state)},
		{"fatal_error", nullptr},
		{"diagnostics", json::array()},
		{"partial_reasons", json::array()},
		{"unevaluable_processes", query.unevaluable_processes}
	};
	for (const auto& process : query.processes) result["processes"].push_back(process_json(process));
	for (const auto& diagnostic : query.diagnostics) result["diagnostics"].push_back(error_json(diagnostic, "query_result", "global"));
	if (query.fatal_error.has_value()) result["fatal_error"] = error_json(*query.fatal_error, "fatal_error", "global", 0, nullptr, nullptr, "fatal");
	if (query.state == ResultState::partial) result["partial_reasons"].push_back("target_inspection");
	return result;
}

void set_error(pd_error_v1* target, uint32_t domain_value, uint32_t code, const char* operation, const char* message) {
	if (target == nullptr) return;
	target->domain = domain_value;
	target->native_code = code;
	const auto copy = [](char* buffer, uint32_t capacity, uint32_t* size, const char* value) {
		const uint32_t required = static_cast<uint32_t>(std::strlen(value) + 1u);
		if (size != nullptr) *size = required;
		if (buffer != nullptr && capacity > 0) {
			const uint32_t count = (required < capacity) ? required : capacity;
			std::memcpy(buffer, value, count);
			buffer[count - 1] = '\0';
		}
	};
	copy(target->operation_utf8, target->operation_capacity, &target->operation_size, operation);
	copy(target->message_utf8, target->message_capacity, &target->message_size, message);
}

bool valid_struct(const pd_query_options_v1* options) {
	return options != nullptr && options->struct_size >= PD_QUERY_OPTIONS_V1_MIN_SIZE && options->abi_version == PD_ABI_VERSION_1;
}

pd_status query(pd_catalog_handle catalog, const pd_query_options_v1* options, pd_result_handle* result, uint32_t pid, bool by_pid) {
	if (result == nullptr || catalog == nullptr || !valid_struct(options)) return PD_INVALID_ARGUMENT;
	*result = nullptr;
	std::unique_lock<std::mutex> catalog_lock(catalog->mutex);
	catalog->active.fetch_add(1);
	std::unique_lock<std::mutex> cancellation_lock;
	if (options->cancellation != nullptr) {
		cancellation_lock = std::unique_lock<std::mutex>(options->cancellation->mutex);
		options->cancellation->active.fetch_add(1);
	}
	auto finish = [&] { if (options->cancellation != nullptr) options->cancellation->active.fetch_sub(1); catalog->active.fetch_sub(1); };
	try {
#if defined(PD_TESTING)
		{
			std::unique_lock<std::mutex> testing_lock(catalog->testing_mutex);
			if (catalog->testing_block_query) {
				catalog->testing_query_blocked = true;
				catalog->testing_condition.notify_all();
				catalog->testing_condition.wait(testing_lock, [&] { return catalog->testing_release_query; });
				catalog->testing_block_query = false;
				catalog->testing_query_blocked = false;
				catalog->testing_release_query = false;
			}
		}
#endif
		InspectionOptions native_options{};
		native_options.include_image_path = options->include_image_path != 0;
		native_options.include_internal_name = options->include_internal_name != 0;
		native_options.include_signature = options->include_signature != 0;
		native_options.include_modules = options->include_modules != 0;
		native_options.allow_remote_file_inspection = options->allow_remote_file_inspection != 0;
		StopToken stop([handle = options->cancellation] { return handle != nullptr && handle->requested.load(); });
		native_options.stop_token = options->cancellation != nullptr ? &stop : nullptr;
		QueryResult native_result = by_pid ? catalog->native->by_pid(pid, native_options) : catalog->native->enumerate(native_options);
		if (catalog->fail_next_json.exchange(false)) {
			catalog->last_operation = by_pid ? "by_pid" : "enumerate";
			catalog->last_code = PD_SERIALIZATION_FAILED;
			catalog->last_message = "test-injected JSON serialization failure";
			finish();
			return PD_SERIALIZATION_FAILED;
		}
		auto output = std::make_unique<pd_result_handle_t>();
		json serialized = query_json(native_result);
		const bool cancelled = options->cancellation != nullptr && options->cancellation->requested.load();
		if (cancelled && serialized["state"] != "failed") {
			if (serialized["state"] == "complete") serialized["state"] = "partial";
			if (serialized["state"] == "partial" &&
				std::find(serialized["partial_reasons"].begin(), serialized["partial_reasons"].end(), "cancellation") == serialized["partial_reasons"].end())
				serialized["partial_reasons"].push_back("cancellation");
		}
		output->json_utf8 = serialized.dump();
		*result = output.release();
		finish();
		if (native_result.state == ResultState::failed) return PD_RESULT_FAILED;
		return cancelled ? PD_CANCELLED : PD_OK;
	} catch (const std::exception& exception) {
		catalog->last_operation = by_pid ? "by_pid" : "enumerate";
		catalog->last_code = ERROR_EXCEPTION_IN_SERVICE;
		catalog->last_message = exception.what();
		finish();
		return PD_NATIVE_EXCEPTION;
	} catch (...) {
		catalog->last_operation = by_pid ? "by_pid" : "enumerate";
		catalog->last_code = ERROR_EXCEPTION_IN_SERVICE;
		catalog->last_message = "native exception";
		finish();
		return PD_NATIVE_EXCEPTION;
	}
}

}

PD_API pd_status PD_CALL pd_cancellation_create_v1(pd_cancellation_handle* cancellation) {
	if (cancellation == nullptr) return PD_INVALID_ARGUMENT;
	*cancellation = new (std::nothrow) pd_cancellation_handle_t();
	return *cancellation == nullptr ? PD_LIMIT_EXCEEDED : PD_OK;
}

PD_API void PD_CALL pd_cancellation_request_v1(pd_cancellation_handle cancellation) {
	if (cancellation != nullptr) cancellation->requested.store(true);
}

PD_API pd_status PD_CALL pd_cancellation_free_v1(pd_cancellation_handle cancellation) {
	if (cancellation == nullptr) return PD_OK;
	// Non-blocking free: never wait for in-flight operations.
	std::unique_lock<std::mutex> lock(cancellation->mutex, std::try_to_lock);
	if (!lock.owns_lock()) return PD_BUSY;
	// Keep explicit PD_BUSY while a query still references this handle.
	if (cancellation->active.load() != 0) return PD_BUSY;
	delete cancellation;
	return PD_OK;
}

#if defined(PD_TESTING)
PD_API void PD_CALL pd_testing_fail_next_json_v1(pd_catalog_handle catalog) {
	if (catalog != nullptr) catalog->fail_next_json.store(true);
}

PD_API void PD_CALL pd_testing_block_query_v1(pd_catalog_handle catalog) {
	if (catalog == nullptr) return;
	std::lock_guard<std::mutex> lock(catalog->testing_mutex);
	catalog->testing_block_query = true;
	catalog->testing_query_blocked = false;
	catalog->testing_release_query = false;
}

PD_API pd_status PD_CALL pd_testing_wait_query_blocked_v1(pd_catalog_handle catalog, uint32_t timeout_ms) {
	if (catalog == nullptr) return PD_INVALID_ARGUMENT;
	std::unique_lock<std::mutex> lock(catalog->testing_mutex);
	if (!catalog->testing_condition.wait_for(lock, std::chrono::milliseconds(timeout_ms), [&] { return catalog->testing_query_blocked; }))
		return PD_BUSY;
	return PD_OK;
}

PD_API void PD_CALL pd_testing_release_query_v1(pd_catalog_handle catalog) {
	if (catalog == nullptr) return;
	{
		std::lock_guard<std::mutex> lock(catalog->testing_mutex);
		catalog->testing_release_query = true;
	}
	catalog->testing_condition.notify_all();
}
#endif

PD_API pd_status PD_CALL pd_catalog_create_v1(pd_abi_version abi_version, pd_catalog_handle* catalog, pd_error_v1* error) {
	if (catalog == nullptr || abi_version != PD_ABI_VERSION_1) return PD_INVALID_ARGUMENT;
	*catalog = nullptr;
	if (!process_discovery_bridge::EnsureNativeLoaded()) {
		set_error(error, 0, ERROR_MOD_NOT_FOUND, "catalog_create", process_discovery_bridge::LastLoaderError());
		return PD_LOAD_FAILED;
	}
	try {
		auto handle = std::make_unique<pd_catalog_handle_t>();
		handle->native = std::make_unique<ProcessCatalog>();
		*catalog = handle.release();
		return PD_OK;
	} catch (const std::bad_alloc&) {
		set_error(error, 6, ERROR_NOT_ENOUGH_MEMORY, "catalog_create", "allocation failed");
		return PD_LIMIT_EXCEEDED;
	} catch (...) {
		set_error(error, 6, ERROR_EXCEPTION_IN_SERVICE, "catalog_create", "native exception");
		return PD_NATIVE_EXCEPTION;
	}
}

PD_API pd_status PD_CALL pd_catalog_free_v1(pd_catalog_handle catalog) {
	if (catalog == nullptr) return PD_OK;
	// Non-blocking free: never wait for in-flight operations.
	std::unique_lock<std::mutex> lock(catalog->mutex, std::try_to_lock);
	if (!lock.owns_lock()) return PD_BUSY;
	// Keep explicit PD_BUSY while a query is active.
	if (catalog->active.load() != 0) return PD_BUSY;
	delete catalog;
	return PD_OK;
}

PD_API pd_status PD_CALL pd_query_enumerate_v1(pd_catalog_handle catalog, const pd_query_options_v1* options, pd_result_handle* result) {
	return query(catalog, options, result, 0, false);
}

PD_API pd_status PD_CALL pd_query_by_pid_v1(pd_catalog_handle catalog, uint32_t pid, const pd_query_options_v1* options, pd_result_handle* result) {
	return query(catalog, options, result, pid, true);
}

PD_API pd_status PD_CALL pd_result_json_size_v1(pd_result_handle result, uint64_t* required_bytes) {
	if (result == nullptr || required_bytes == nullptr) return PD_INVALID_ARGUMENT;
	*required_bytes = static_cast<uint64_t>(result->json_utf8.size() + 1u);
	return PD_OK;
}

PD_API pd_status PD_CALL pd_result_copy_json_v1(pd_result_handle result, char* buffer, uint64_t capacity, uint64_t* written_bytes) {
	if (result == nullptr || written_bytes == nullptr) return PD_INVALID_ARGUMENT;
	const uint64_t required = static_cast<uint64_t>(result->json_utf8.size() + 1u);
	*written_bytes = required;
	if (buffer == nullptr || capacity < required) return PD_BUFFER_TOO_SMALL;
	std::memcpy(buffer, result->json_utf8.data(), result->json_utf8.size());
	buffer[result->json_utf8.size()] = '\0';
	return PD_OK;
}

PD_API void PD_CALL pd_result_free_v1(pd_result_handle result) {
	delete result;
}

PD_API pd_status PD_CALL pd_get_last_error_v1(pd_catalog_handle catalog, pd_error_v1* error) {
	if (catalog == nullptr || error == nullptr) return PD_INVALID_ARGUMENT;
	set_error(error, 6, catalog->last_code, catalog->last_operation.c_str(), catalog->last_message.c_str());
	return PD_OK;
}
