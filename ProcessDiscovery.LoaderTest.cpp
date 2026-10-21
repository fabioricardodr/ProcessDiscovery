#include <Windows.h>
#include <winver.h>
#include <iostream>
#include <string>
#include <vector>

static bool HasExpectedFileVersion(const wchar_t* path) {
	DWORD ignored = 0;
	const DWORD size = GetFileVersionInfoSizeW(path, &ignored);
	if (size == 0) return false;
	std::vector<BYTE> data(size);
	if (!GetFileVersionInfoW(path, 0, size, data.data())) return false;
	VS_FIXEDFILEINFO* info = nullptr;
	UINT length = 0;
	if (!VerQueryValueW(data.data(), L"\\", reinterpret_cast<LPVOID*>(&info), &length) ||
		info == nullptr || length < sizeof(VS_FIXEDFILEINFO)) return false;
	return HIWORD(info->dwFileVersionMS) == 1 &&
		LOWORD(info->dwFileVersionMS) == 0 &&
		HIWORD(info->dwFileVersionLS) == 0 &&
		LOWORD(info->dwFileVersionLS) == 0;
}

int wmain() {
	HMODULE module = LoadLibraryW(L"ProcessDiscovery.dll");
	if (module == nullptr) {
		std::wcerr << L"FAIL: LoadLibraryW(ProcessDiscovery.dll), erro " << GetLastError() << L"\n";
		return 2;
	}
	if (!HasExpectedFileVersion(L"ProcessDiscovery.dll")) {
		std::cerr << "FAIL: VERSIONINFO/VS_FIXEDFILEINFO ausente ou divergente.\n";
		FreeLibrary(module);
		return 5;
	}

#if defined(_WIN64)
	constexpr char enumerate_symbol[] = "?enumerate@ProcessCatalog@@QEAA?AUQueryResult@@AEBUInspectionOptions@@@Z";
#else
	constexpr char enumerate_symbol[] = "?enumerate@ProcessCatalog@@QAE?AUQueryResult@@ABUInspectionOptions@@@Z";
#endif
	FARPROC exported = GetProcAddress(module, enumerate_symbol);
	const bool released = FreeLibrary(module) != FALSE;
	if (exported == nullptr) {
		std::cerr << "FAIL: export ProcessCatalog::enumerate não encontrado.\n";
		return 3;
	}
	if (!released) {
		std::cerr << "FAIL: FreeLibrary falhou, erro " << GetLastError() << "\n";
		return 4;
	}
	std::cout << "PASS: DLL carregada, export verificado e DLL liberada.\n";
	return 0;
}
