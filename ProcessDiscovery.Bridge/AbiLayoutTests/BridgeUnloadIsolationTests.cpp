#include <Windows.h>
#include <cassert>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <cwchar>
#include <vector>
#include "../include/process_discovery_bridge.h"

template<class T> static T symbol(HMODULE module, const char* name) {
	return reinterpret_cast<T>(GetProcAddress(module, name));
}

int main() {
	wchar_t executable_path[MAX_PATH]{};
	const DWORD executable_length = GetModuleFileNameW(nullptr, executable_path, MAX_PATH);
	if (executable_length == 0 || executable_length >= MAX_PATH) return 9;
	wchar_t* separator = wcsrchr(executable_path, L'\\');
	if (separator == nullptr) return 9;
	*(separator + 1) = L'\0';
	wcscat_s(executable_path, L"ProcessDiscovery.Bridge.dll");
	HMODULE bridge = LoadLibraryExW(executable_path, nullptr,
		LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR | LOAD_LIBRARY_SEARCH_SYSTEM32);
	if (bridge == nullptr) return 10;
	auto create = symbol<decltype(&pd_catalog_create_v1)>(bridge, "pd_catalog_create_v1");
	auto query = symbol<decltype(&pd_query_enumerate_v1)>(bridge, "pd_query_enumerate_v1");
	auto free_catalog = symbol<decltype(&pd_catalog_free_v1)>(bridge, "pd_catalog_free_v1");
	auto size = symbol<decltype(&pd_result_json_size_v1)>(bridge, "pd_result_json_size_v1");
	auto copy = symbol<decltype(&pd_result_copy_json_v1)>(bridge, "pd_result_copy_json_v1");
	auto free_result = symbol<decltype(&pd_result_free_v1)>(bridge, "pd_result_free_v1");
	if (!create || !query || !free_catalog || !size || !copy || !free_result) return 11;

	pd_catalog_handle catalog = nullptr;
	if (create(PD_ABI_VERSION_1, &catalog, nullptr) != PD_OK) return 12;
	pd_query_options_v1 options{};
	options.struct_size = sizeof(options);
	options.abi_version = PD_ABI_VERSION_1;
	pd_result_handle result = nullptr;
	const pd_status query_status = query(catalog, &options, &result);
	if ((query_status != PD_OK && query_status != PD_RESULT_FAILED && query_status != PD_CANCELLED) || result == nullptr) return 13;
	if (free_catalog(catalog) != PD_OK) return 14;

	HMODULE native = GetModuleHandleW(L"ProcessDiscovery.dll");
	if (native == nullptr || !FreeLibrary(native)) return 15;
	if (GetModuleHandleW(L"ProcessDiscovery.dll") != nullptr) return 16;

	uint64_t required = 0;
	if (size(result, &required) != PD_OK || required == 0) return 17;
	std::vector<char> buffer(static_cast<size_t>(required));
	uint64_t written = 0;
	if (copy(result, buffer.data(), required, &written) != PD_OK || std::strstr(buffer.data(), "\"schema_version\":3") == nullptr) return 18;
	free_result(result);
	if (!FreeLibrary(bridge)) return 19;
	std::puts("unload isolation passed");
	return 0;
}
