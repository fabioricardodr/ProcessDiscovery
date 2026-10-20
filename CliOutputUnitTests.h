#pragma once

#include "CliOutput.h"

inline bool RunCliOutputUnitTests() {
	const ParseResult normal = ArgumentParser::parse({L"list", L"--modules", L"--internal-name", L"--signature"});
	if (!normal.ok || normal.options.output_mode != OutputMode::normal || !normal.options.include_modules ||
		!normal.options.include_internal_name || !normal.options.include_signature) return false;

	const ParseResult advanced = ArgumentParser::parse({L"list", L"--advanced", L"--signature"});
	if (!advanced.ok || advanced.options.output_mode != OutputMode::advanced) return false;
	if (ArgumentParser::parse({L"query-pid", L"1", L"--advanced"}).ok) return false;

	ProcessRecord process;
	process.pid = 4120;
	process.name = Field<std::wstring>::available(L"caf\u00e9");
	process.signature = Field<SignatureStatus>::available({SignaturePresence::present, TrustState::invalid, 1, std::nullopt, L""});
	process.internal_name = Field<std::wstring>::unavailable();
	CollectionResult<ModuleRecord> modules;
	process.modules = Field<CollectionResult<ModuleRecord>>::available(modules);

	QueryResult result;
	result.processes.push_back(process);
	CliOptions flags;
	flags.include_signature = true;
	flags.include_internal_name = true;
	flags.include_modules = true;
	const std::string name = Utf8FromWide(L"caf\u00e9");
	if (NormalFormatter::format(result, flags) != "4120\t" + name + "\ttrue\tfalse\tfalse\n") return false;

	process.signature = Field<SignatureStatus>::available({SignaturePresence::no_embedded_signature, TrustState::not_evaluated, 0, std::nullopt, L""});
	process.internal_name = Field<std::wstring>::available(L"Internal");
	modules.state = ResultState::partial;
	process.modules = Field<CollectionResult<ModuleRecord>>::available(modules);
	result.processes.front() = process;
	if (NormalFormatter::format(result, flags) != "4120\t" + name + "\tfalse\ttrue\terror\n") return false;

	process.signature = Field<SignatureStatus>::available({SignaturePresence::unknown, TrustState::indeterminate, 0, std::nullopt, L""});
	process.internal_name = Field<std::wstring>::available_unverified(L"Internal");
	process.modules = Field<CollectionResult<ModuleRecord>>::unavailable();
	result.processes.front() = process;
	if (NormalFormatter::format(result, flags) != "4120\t" + name + "\terror\terror\terror\n") return false;

	CliOptions noFlags;
	const std::string minimal = NormalFormatter::format(result, noFlags);
	return minimal == "4120\t" + name + "\n" && LegacyFormatter::format(result) == minimal;
}
