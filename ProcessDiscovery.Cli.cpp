#include "ProcessDiscovery/ProcessCatalog.h"
#include <Windows.h>
#include <atomic>
#include <iostream>
#include <limits>
#include <string>
#include <string_view>

namespace {
std::atomic_bool cancelled = false;
BOOL WINAPI ConsoleHandler(DWORD type) {
	if (type == CTRL_C_EVENT || type == CTRL_BREAK_EVENT || type == CTRL_CLOSE_EVENT) { cancelled.store(true, std::memory_order_relaxed); return TRUE; }
	return FALSE;
}
void WriteText(const std::wstring& text) {
	HANDLE output = GetStdHandle(STD_OUTPUT_HANDLE); DWORD mode = 0;
	if (output != INVALID_HANDLE_VALUE && GetConsoleMode(output, &mode)) { DWORD written = 0; WriteConsoleW(output, text.data(), static_cast<DWORD>(text.size()), &written, nullptr); return; }
	int size = WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, text.data(), static_cast<int>(text.size()), nullptr, 0, nullptr, nullptr);
	if (size <= 0) return; std::string utf8(static_cast<std::size_t>(size), '\0');
	WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, text.data(), static_cast<int>(text.size()), utf8.data(), size, nullptr, nullptr); std::cout << utf8;
}
void Help() { WriteText(L"ProcessDiscovery\nUso: list | query-pid <pid> | query-name <nome> | query-path <caminho> [--modules] [--signature] [--internal-name] [--no-image-path]\nAvisos: PPL, permissões, cross-bitness, volumes remotos, corridas de processo e bloqueios de módulos podem produzir resultado parcial.\nno_embedded_signature significa que não foi encontrado um certificado embutido; não é falha global.\n"); }
int ResultCode(const QueryResult& result) { if (result.state == ResultState::failed) return 2; if (result.state == ResultState::partial || cancelled.load(std::memory_order_relaxed)) return 4; return result.processes.empty() ? 3 : 0; }
bool TryParsePid(std::wstring_view text, std::uint32_t& pid) {
	if (text.empty() || text.front() == L'-') return false;
	try {
		const unsigned long parsed = std::stoul(std::wstring(text));
		if (parsed > (std::numeric_limits<std::uint32_t>::max)()) return false;
		pid = static_cast<std::uint32_t>(parsed);
		return true;
	}
	catch (const std::invalid_argument&) { return false; }
	catch (const std::out_of_range&) { return false; }
}
}
int wmain(int argc, wchar_t** argv) {
	SetConsoleCtrlHandler(ConsoleHandler, TRUE);
	if (argc < 2 || std::wstring_view(argv[1]) == L"--help") { Help(); return argc < 2 ? 1 : 0; }
	ProcessCatalog catalog; InspectionOptions options; QueryResult result; const std::wstring command = argv[1];
	const StopToken stop_token([] { return cancelled.load(std::memory_order_relaxed); });
	options.stop_token = &stop_token;
	for (int index = 2; index < argc; ++index) {
		const std::wstring flag = argv[index];
		if (flag == L"--modules") options.include_modules = true;
		else if (flag == L"--signature") options.include_signature = true;
		else if (flag == L"--internal-name") options.include_internal_name = true;
		else if (flag == L"--no-image-path") options.include_image_path = false;
		else { Help(); return 1; }
	}
	if (command == L"list") result = catalog.enumerate(options);
	else if (command == L"query-pid" && argc >= 3) { std::uint32_t pid = 0; if (!TryParsePid(argv[2], pid)) return 1; result = catalog.by_pid(pid, options); }
	else if (command == L"query-name" && argc >= 3) result = catalog.by_name(argv[2], options);
	else if (command == L"query-path" && argc >= 3) result = catalog.by_full_path(argv[2], options);
	else { Help(); return 1; }
	for (const ProcessRecord& process : result.processes) WriteText(std::to_wstring(process.pid) + L"\t" + process.name.value().value_or(L"<indisponível>") + L"\n");
	return ResultCode(result);
}
