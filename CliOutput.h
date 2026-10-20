#pragma once

#include "ProcessDiscovery/ProcessTypes.h"
#include <functional>
#include <string>
#include <string_view>
#include <vector>

#include <nlohmann/json.hpp>

enum class CliFormat { tsv, json, legacy };

enum class CliCommand { list, query_pid, query_name, query_path };

struct CliOptions {
	CliCommand command = CliCommand::list;
	CliFormat format = CliFormat::tsv;
	std::uint32_t pid = 0;
	std::wstring argument;
	bool has_argument = false;
	bool include_modules = false;
	bool include_signature = false;
	bool include_internal_name = false;
	bool include_image_path = true;
};

struct ParseResult {
	bool ok = false;
	bool help = false;
	CliOptions options;
	std::string error;
};

class ArgumentParser final {
public:
	static ParseResult parse(const std::vector<std::wstring>& arguments);
};

class TabularFormatter final {
public:
	static std::string format(const QueryResult& result, const CliOptions& options);
	static std::string summary(const QueryResult& result);
};

class JsonFormatter final {
public:
	static std::string format(const QueryResult& result, const CliOptions& options);
};

class LegacyFormatter final {
public:
	static std::string format(const QueryResult& result);
};

class OutputSink {
public:
	using Writer = std::function<bool(std::string_view)>;

	OutputSink(Writer stdout_writer, Writer stderr_writer);
	bool write_stdout(std::string_view text) const;
	bool write_stderr(std::string_view text) const;

private:
	Writer stdout_writer_;
	Writer stderr_writer_;
};

OutputSink MakeConsoleOutputSink();

class ExitCodeMapper final {
public:
	static int map(bool parsing_valid, bool output_ok, bool cancelled, const QueryResult* result);
};

std::string Utf8FromWide(std::wstring_view value);
