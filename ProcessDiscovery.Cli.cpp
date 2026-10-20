#include "CliOutput.h"
#include "ProcessDiscovery/ProcessCatalog.h"
#include <Windows.h>
#include <atomic>
#include <string>
#include <vector>

namespace {
std::atomic_bool cancelled = false;

BOOL WINAPI ConsoleHandler(DWORD type) {
	if (type == CTRL_C_EVENT || type == CTRL_BREAK_EVENT || type == CTRL_CLOSE_EVENT) {
		cancelled.store(true, std::memory_order_relaxed);
		return TRUE;
	}
	return FALSE;
}
}

int wmain(int argc, wchar_t** argv) {
	SetConsoleCtrlHandler(ConsoleHandler, TRUE);
	std::vector<std::wstring> arguments;
	for (int index = 1; index < argc; ++index) arguments.emplace_back(argv[index]);

	const ParseResult parsed = ArgumentParser::parse(arguments);
	if (!parsed.ok) {
		OutputSink sink = MakeConsoleOutputSink();
		sink.write_stderr("parse_error=" + parsed.error + "\n");
		return ExitCodeMapper::map(false, true, false, nullptr);
	}
	if (parsed.help) {
		OutputSink sink = MakeConsoleOutputSink();
		sink.write_stdout("ProcessDiscovery\nUso: list|query-pid <pid>|query-name <nome>|query-path <caminho> [flags] [--format tsv|json|legacy]\n");
		return 0;
	}

	ProcessCatalog catalog;
	InspectionOptions inspection;
	inspection.include_image_path = parsed.options.include_image_path;
	inspection.include_internal_name = parsed.options.include_internal_name;
	inspection.include_signature = parsed.options.include_signature;
	inspection.include_modules = parsed.options.include_modules;
	const StopToken stop_token([] { return cancelled.load(std::memory_order_relaxed); });
	inspection.stop_token = &stop_token;

	QueryResult result;
	switch (parsed.options.command) {
	case CliCommand::list:
		result = catalog.enumerate(inspection);
		break;
	case CliCommand::query_pid:
		result = catalog.by_pid(parsed.options.pid, inspection);
		break;
	case CliCommand::query_name:
		result = catalog.by_name(parsed.options.argument, inspection);
		break;
	case CliCommand::query_path:
		result = catalog.by_full_path(parsed.options.argument, inspection);
		break;
	}

	std::string output;
	try {
		switch (parsed.options.format) {
		case CliFormat::tsv:
			output = TabularFormatter::format(result, parsed.options);
			break;
		case CliFormat::json:
			output = JsonFormatter::format(result, parsed.options);
			output.push_back('\n');
			break;
		case CliFormat::legacy:
			output = LegacyFormatter::format(result);
			break;
		}
	}
	catch (const std::exception& error) {
		OutputSink sink = MakeConsoleOutputSink();
		sink.write_stderr(std::string("serialization_error=") + error.what() + "\n");
		return ExitCodeMapper::map(true, false, cancelled.load(std::memory_order_relaxed), &result);
	}

	OutputSink sink = MakeConsoleOutputSink();
	bool output_ok = sink.write_stdout(output);
	if (parsed.options.format == CliFormat::tsv ||
		(parsed.options.format == CliFormat::legacy && (!result.diagnostics.empty() || result.fatal_error.has_value()))) {
		output_ok = sink.write_stderr(TabularFormatter::summary(result)) && output_ok;
	}
	if (!output_ok) sink.write_stderr("output_error=failed to write stdout or stderr\n");
	return ExitCodeMapper::map(true, output_ok, cancelled.load(std::memory_order_relaxed), &result);
}