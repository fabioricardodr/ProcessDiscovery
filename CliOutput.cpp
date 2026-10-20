#include "CliOutput.h"

#define NOMINMAX
#include <Windows.h>
#include <algorithm>
#include <iostream>
#include <limits>
#include <sstream>
#include <stdexcept>

namespace {
using json = nlohmann::json;

const char* FieldStateName(FieldState state) {
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

const char* ResultStateName(ResultState state) {
	switch (state) {
	case ResultState::complete: return "complete";
	case ResultState::partial: return "partial";
	case ResultState::failed: return "failed";
	}
	return "failed";
}

const char* ErrorDomainName(ErrorDomain domain) {
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

const char* PresenceName(SignaturePresence value) {
	switch (value) {
	case SignaturePresence::present: return "present";
	case SignaturePresence::no_embedded_signature: return "no_embedded_signature";
	case SignaturePresence::unknown: return "unknown";
	}
	return "unknown";
}

const char* TrustName(TrustState value) {
	switch (value) {
	case TrustState::valid: return "valid";
	case TrustState::invalid: return "invalid";
	case TrustState::indeterminate: return "indeterminate";
	case TrustState::not_evaluated: return "not_evaluated";
	}
	return "indeterminate";
}

const char* ResultStateName(const std::optional<ResultState>& state) {
	return state.has_value() ? ResultStateName(*state) : nullptr;
}

json ErrorJson(const ErrorInfo& error) {
	return json{
		{"operation", error.operation},
		{"domain", ErrorDomainName(error.domain)},
		{"native_code", error.native_code},
		{"message", error.message}
	};
}

std::string DiagnosticCell(const std::optional<ErrorInfo>& error) {
	return error.has_value() ? ErrorJson(*error).dump() : "-";
}

std::string DiagnosticsCell(const std::vector<ErrorInfo>& errors) {
	if (errors.empty()) return "-";
	json values = json::array();
	for (const ErrorInfo& error : errors) values.push_back(ErrorJson(error));
	return values.dump();
}

std::string EscapeTsv(std::string_view value) {
	std::string result;
	result.reserve(value.size());
	for (const char ch : value) {
		switch (ch) {
		case '\\': result += "\\\\"; break;
		case '\t': result += "\\t"; break;
		case '\n': result += "\\n"; break;
		case '\r': result += "\\r"; break;
		default: result.push_back(ch); break;
		}
	}
	return result;
}

template<class T>
std::string FieldText(const Field<T>& field, const std::function<std::string(const T&)>& render) {
	return field.value().has_value() ? EscapeTsv(render(*field.value())) : "-";
}

template<class T>
json FieldValue(const Field<T>& field, const std::function<json(const T&)>& render) {
	return field.value().has_value() ? render(*field.value()) : json(nullptr);
}

json ModuleJson(const ModuleRecord& module) {
	return json{
		{"name", FieldValue<std::wstring>(module.name, [](const std::wstring& value) { return json(Utf8FromWide(value)); })},
		{"name_state", FieldStateName(module.name.state())},
		{"name_diagnostic", module.name.error().has_value() ? ErrorJson(*module.name.error()) : json(nullptr)},
		{"path", FieldValue<std::wstring>(module.path, [](const std::wstring& value) { return json(Utf8FromWide(value)); })},
		{"path_state", FieldStateName(module.path.state())},
		{"path_diagnostic", module.path.error().has_value() ? ErrorJson(*module.path.error()) : json(nullptr)},
		{"base_address", module.base_address}
	};
}

json ProcessJson(const ProcessRecord& process, const CliOptions& options) {
	json value = {
		{"pid", process.pid},
		{"name", FieldValue<std::wstring>(process.name, [](const std::wstring& text) { return json(Utf8FromWide(text)); })},
		{"name_state", FieldStateName(process.name.state())},
		{"image_path", FieldValue<std::wstring>(process.image_path, [](const std::wstring& text) { return json(Utf8FromWide(text)); })},
		{"image_path_state", FieldStateName(process.image_path.state())},
		{"diagnostics", json::array()},
		{"modules", nullptr},
		{"modules_count", nullptr},
		{"modules_state", FieldStateName(FieldState::not_requested)},
		{"modules_result_state", nullptr},
		{"modules_diagnostics", json::array()}
	};
	if (options.include_internal_name) {
		value["internal_name"] = FieldValue<std::wstring>(process.internal_name, [](const std::wstring& text) { return json(Utf8FromWide(text)); });
		value["internal_name_state"] = FieldStateName(process.internal_name.state());
		value["internal_name_diagnostic"] = process.internal_name.error().has_value() ? ErrorJson(*process.internal_name.error()) : json(nullptr);
	}
	if (options.include_signature) {
		json signature = {
			{"presence", nullptr}, {"trust", nullptr}, {"wintrust_status", nullptr},
			{"last_error", nullptr}, {"policy", nullptr}, {"state", FieldStateName(process.signature.state())},
			{"diagnostic", process.signature.error().has_value() ? ErrorJson(*process.signature.error()) : json(nullptr)}
		};
		if (process.signature.value().has_value()) {
			const SignatureStatus& status = *process.signature.value();
			signature["presence"] = PresenceName(status.presence);
			signature["trust"] = TrustName(status.trust);
			signature["wintrust_status"] = status.wintrust_status;
			signature["last_error"] = status.last_error.has_value() ? json(*status.last_error) : json(nullptr);
			signature["policy"] = Utf8FromWide(status.policy);
		}
		value["signature"] = std::move(signature);
	}
	for (const ErrorInfo& error : process.diagnostics) value["diagnostics"].push_back(ErrorJson(error));
	if (options.include_modules) {
		value["modules_state"] = FieldStateName(process.modules.state());
		if (process.modules.value().has_value()) {
			const CollectionResult<ModuleRecord>& collection = *process.modules.value();
			value["modules"] = json::array();
			for (const ModuleRecord& module : collection.items) value["modules"].push_back(ModuleJson(module));
			value["modules_count"] = collection.items.size();
			value["modules_result_state"] = ResultStateName(collection.state);
			value["modules_diagnostics"] = json::array();
			for (const ErrorInfo& error : collection.diagnostics) value["modules_diagnostics"].push_back(ErrorJson(error));
		}
	}
	return value;
}

std::string ValueForTsv(const Field<std::wstring>& field) {
	return FieldText<std::wstring>(field, [](const std::wstring& value) { return Utf8FromWide(value); });
}

std::string SignatureValue(const Field<SignatureStatus>& field, const std::function<std::string(const SignatureStatus&)>& render) {
	return field.value().has_value() ? EscapeTsv(render(*field.value())) : "-";
}

std::string FormatHelp() {
	return "ProcessDiscovery\nUso: list|query-pid <pid>|query-name <nome>|query-path <caminho> [flags] [--format tsv|json|legacy]\n";
}
}

std::string Utf8FromWide(std::wstring_view value) {
	if (value.empty()) return {};
	const int size = WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, value.data(), static_cast<int>(value.size()), nullptr, 0, nullptr, nullptr);
	if (size <= 0) throw std::runtime_error("falha ao converter texto para UTF-8");
	std::string result(static_cast<std::size_t>(size), '\0');
	if (WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, value.data(), static_cast<int>(value.size()), result.data(), size, nullptr, nullptr) <= 0) {
		throw std::runtime_error("falha ao converter texto para UTF-8");
	}
	return result;
}

ParseResult ArgumentParser::parse(const std::vector<std::wstring>& args) {
	ParseResult parsed;
	if (args.empty()) { parsed.error = "subcomando ausente"; return parsed; }
	if (args.size() == 1 && args.front() == L"--help") { parsed.ok = true; parsed.help = true; return parsed; }
	if (args.front() == L"list") parsed.options.command = CliCommand::list;
	else if (args.front() == L"query-pid") parsed.options.command = CliCommand::query_pid;
	else if (args.front() == L"query-name") parsed.options.command = CliCommand::query_name;
	else if (args.front() == L"query-path") parsed.options.command = CliCommand::query_path;
	else { parsed.error = "subcomando desconhecido"; return parsed; }

	for (std::size_t index = 1; index < args.size(); ++index) {
		const std::wstring& arg = args[index];
		if (arg == L"--modules") parsed.options.include_modules = true;
		else if (arg == L"--signature") parsed.options.include_signature = true;
		else if (arg == L"--internal-name") parsed.options.include_internal_name = true;
		else if (arg == L"--no-image-path") parsed.options.include_image_path = false;
		else if (arg == L"--format") {
			if (++index >= args.size()) { parsed.error = "valor ausente para --format"; return parsed; }
			if (args[index] == L"tsv") parsed.options.format = CliFormat::tsv;
			else if (args[index] == L"json") parsed.options.format = CliFormat::json;
			else if (args[index] == L"legacy") parsed.options.format = CliFormat::legacy;
			else { parsed.error = "formato desconhecido"; return parsed; }
		}
		else if (arg.starts_with(L"--")) { parsed.error = "flag desconhecida"; return parsed; }
		else if (parsed.options.command == CliCommand::list || parsed.options.has_argument) { parsed.error = "argumento posicional inesperado"; return parsed; }
		else {
			parsed.options.argument = arg;
			parsed.options.has_argument = true;
		}
	}
	if (parsed.options.command != CliCommand::list && !parsed.options.has_argument) { parsed.error = "argumento posicional ausente"; return parsed; }
	if (parsed.options.command == CliCommand::query_pid) {
		try {
			std::size_t consumed = 0;
			const unsigned long long value = std::stoull(parsed.options.argument, &consumed);
			if (consumed != parsed.options.argument.size() || value > (std::numeric_limits<std::uint32_t>::max)()) throw std::invalid_argument("pid");
			parsed.options.pid = static_cast<std::uint32_t>(value);
		}
		catch (...) { parsed.error = "pid inválido"; return parsed; }
	}
	parsed.ok = true;
	return parsed;
}

std::string TabularFormatter::format(const QueryResult& result, const CliOptions& options) {
	std::vector<std::string> columns = {"pid", "name", "name_state", "image_path", "image_path_state"};
	if (options.include_internal_name) columns.insert(columns.end(), {"internal_name", "internal_name_state", "internal_name_diagnostic"});
	if (options.include_signature) columns.insert(columns.end(), {"signature_presence", "signature_trust", "signature_wintrust_status", "signature_last_error", "signature_policy", "signature_state", "signature_diagnostic"});
	if (options.include_modules) columns.insert(columns.end(), {"modules_count", "modules_state", "modules_result_state", "modules_diagnostics"});
	std::ostringstream output;
	for (std::size_t i = 0; i < columns.size(); ++i) output << (i == 0 ? "" : "\t") << columns[i];
	output << '\n';
	for (const ProcessRecord& process : result.processes) {
		std::vector<std::string> cells = {
			std::to_string(process.pid), ValueForTsv(process.name), FieldStateName(process.name.state()),
			ValueForTsv(process.image_path), FieldStateName(process.image_path.state())
		};
		if (options.include_internal_name) {
			cells.push_back(ValueForTsv(process.internal_name));
			cells.push_back(FieldStateName(process.internal_name.state()));
			cells.push_back(DiagnosticCell(process.internal_name.error()));
		}
		if (options.include_signature) {
			cells.push_back(SignatureValue(process.signature, [](const SignatureStatus& status) { return PresenceName(status.presence); }));
			cells.push_back(SignatureValue(process.signature, [](const SignatureStatus& status) { return TrustName(status.trust); }));
			cells.push_back(SignatureValue(process.signature, [](const SignatureStatus& status) { return std::to_string(status.wintrust_status); }));
			cells.push_back(process.signature.value().has_value() && process.signature.value()->last_error.has_value() ? std::to_string(*process.signature.value()->last_error) : "-");
			cells.push_back(SignatureValue(process.signature, [](const SignatureStatus& status) { return Utf8FromWide(status.policy); }));
			cells.push_back(FieldStateName(process.signature.state()));
			cells.push_back(DiagnosticCell(process.signature.error()));
		}
		if (options.include_modules) {
			cells.push_back(process.modules.value().has_value() ? std::to_string(process.modules.value()->items.size()) : "-");
			cells.push_back(FieldStateName(process.modules.state()));
			cells.push_back(process.modules.value().has_value() ? ResultStateName(process.modules.value()->state) : "-");
			cells.push_back(process.modules.value().has_value() ? DiagnosticsCell(process.modules.value()->diagnostics) : "-");
		}
		for (std::size_t i = 0; i < cells.size(); ++i) output << (i == 0 ? "" : "\t") << EscapeTsv(cells[i]);
		output << '\n';
	}
	return output.str();
}

std::string TabularFormatter::summary(const QueryResult& result) {
	std::ostringstream output;
	output << "result_state=" << ResultStateName(result.state) << '\n';
	output << "process_count=" << result.processes.size() << '\n';
	output << "unevaluable_processes=" << result.unevaluable_processes << '\n';
	output << "fatal_error=" << (result.fatal_error.has_value() ? ErrorJson(*result.fatal_error).dump() : "null") << '\n';
	for (const ErrorInfo& error : result.diagnostics) output << "diagnostic=" << ErrorJson(error).dump() << '\n';
	return output.str();
}

std::string JsonFormatter::format(const QueryResult& result, const CliOptions& options) {
	json document = {
		{"schema_version", 1}, {"result_state", ResultStateName(result.state)},
		{"process_count", result.processes.size()}, {"unevaluable_processes", result.unevaluable_processes},
		{"processes", json::array()}, {"diagnostics", json::array()},
		{"fatal_error", result.fatal_error.has_value() ? ErrorJson(*result.fatal_error) : json(nullptr)}
	};
	for (const ProcessRecord& process : result.processes) document["processes"].push_back(ProcessJson(process, options));
	for (const ErrorInfo& error : result.diagnostics) document["diagnostics"].push_back(ErrorJson(error));
	return document.dump();
}

std::string LegacyFormatter::format(const QueryResult& result) {
	std::ostringstream output;
	for (const ProcessRecord& process : result.processes) {
		output << process.pid << '\t' << (process.name.value().has_value() ? EscapeTsv(Utf8FromWide(*process.name.value())) : "-") << '\n';
	}
	return output.str();
}

OutputSink::OutputSink(Writer stdout_writer, Writer stderr_writer) : stdout_writer_(std::move(stdout_writer)), stderr_writer_(std::move(stderr_writer)) {}
bool OutputSink::write_stdout(std::string_view text) const { return stdout_writer_ != nullptr && stdout_writer_(text); }
bool OutputSink::write_stderr(std::string_view text) const { return stderr_writer_ != nullptr && stderr_writer_(text); }

OutputSink MakeConsoleOutputSink() {
	return OutputSink(
		[](std::string_view text) { std::cout.write(text.data(), static_cast<std::streamsize>(text.size())); std::cout.flush(); return static_cast<bool>(std::cout); },
		[](std::string_view text) { std::cerr.write(text.data(), static_cast<std::streamsize>(text.size())); std::cerr.flush(); return static_cast<bool>(std::cerr); });
}

int ExitCodeMapper::map(bool parsing_valid, bool output_ok, bool cancelled, const QueryResult* result) {
	if (!parsing_valid) return 1;
	if (!output_ok) return 5;
	if (result == nullptr) return 5;
	if (result->state == ResultState::failed) return 2;
	if (result->state == ResultState::partial || cancelled) return 4;
	return result->processes.empty() ? 3 : 0;
}
