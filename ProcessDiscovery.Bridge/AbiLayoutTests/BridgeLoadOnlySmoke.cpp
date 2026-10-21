#include <Windows.h>
#include <cstdio>

int main() {
	HMODULE bridge = LoadLibraryExW(L"C:\\Users\\fabio\\source\\repos\\ProcessDiscovery\\artifacts\\x64\\ProcessDiscovery.Bridge.dll", nullptr,
		LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR | LOAD_LIBRARY_SEARCH_SYSTEM32);
	std::printf("bridge=%p error=%lu\n", static_cast<void*>(bridge), GetLastError());
	if (!bridge) return 1;
	auto free_result = FreeLibrary(bridge);
	std::printf("free=%d\n", free_result ? 1 : 0);
	return free_result ? 0 : 2;
}
