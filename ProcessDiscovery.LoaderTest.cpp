#include <Windows.h>
#include <iostream>
#include <string>

int wmain() {
	HMODULE module = LoadLibraryW(L"ProcessDiscovery.dll");
	if (module == nullptr) {
		std::wcerr << L"FAIL: LoadLibraryW(ProcessDiscovery.dll), erro " << GetLastError() << L"\n";
		return 2;
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
