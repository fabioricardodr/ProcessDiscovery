#include "pch.h"
#include "Win32Api.h"

namespace {
	NativeWin32Api native_api;
	IWin32Api* active_api = &native_api;
}

IWin32Api& GetWin32Api() noexcept {
	return *active_api;
}

void SetWin32ApiForTesting(IWin32Api* api) noexcept {
	active_api = api != nullptr ? api : &native_api;
}

DWORD NativeWin32Api::GetLastErrorValue() const noexcept { return ::GetLastError(); }
void NativeWin32Api::ClearLastError() noexcept { ::SetLastError(ERROR_SUCCESS); }
HANDLE NativeWin32Api::CreateToolhelp32Snapshot(DWORD flags, DWORD process_id) noexcept { return ::CreateToolhelp32Snapshot(flags, process_id); }
BOOL NativeWin32Api::Process32FirstW(HANDLE snapshot, PROCESSENTRY32W* entry) noexcept { return ::Process32FirstW(snapshot, entry); }
BOOL NativeWin32Api::Process32NextW(HANDLE snapshot, PROCESSENTRY32W* entry) noexcept { return ::Process32NextW(snapshot, entry); }
BOOL NativeWin32Api::Module32FirstW(HANDLE snapshot, MODULEENTRY32W* entry) noexcept { return ::Module32FirstW(snapshot, entry); }
BOOL NativeWin32Api::Module32NextW(HANDLE snapshot, MODULEENTRY32W* entry) noexcept { return ::Module32NextW(snapshot, entry); }
HANDLE NativeWin32Api::OpenProcess(DWORD desired_access, BOOL inherit_handle, DWORD process_id) noexcept { return ::OpenProcess(desired_access, inherit_handle, process_id); }
BOOL NativeWin32Api::CloseHandle(HANDLE handle) noexcept { return ::CloseHandle(handle); }
DWORD NativeWin32Api::WaitForSingleObject(HANDLE handle, DWORD milliseconds) noexcept { return ::WaitForSingleObject(handle, milliseconds); }
BOOL NativeWin32Api::IsWow64Process2(HANDLE process, PUSHORT process_machine, PUSHORT native_machine) noexcept { return ::IsWow64Process2(process, process_machine, native_machine); }
BOOL NativeWin32Api::QueryFullProcessImageNameW(HANDLE process, DWORD flags, LPWSTR buffer, PDWORD size) noexcept { return ::QueryFullProcessImageNameW(process, flags, buffer, size); }
BOOL NativeWin32Api::GetProcessTimes(HANDLE process, LPFILETIME creation, LPFILETIME exit, LPFILETIME kernel, LPFILETIME user) noexcept { return ::GetProcessTimes(process, creation, exit, kernel, user); }
DWORD NativeWin32Api::GetFileVersionInfoSizeW(LPCWSTR path, LPDWORD handle) noexcept { return ::GetFileVersionInfoSizeW(path, handle); }
BOOL NativeWin32Api::GetFileVersionInfoW(LPCWSTR path, DWORD handle, DWORD size, LPVOID data) noexcept { return ::GetFileVersionInfoW(path, handle, size, data); }
BOOL NativeWin32Api::VerQueryValueW(const LPVOID block, LPCWSTR sub_block, LPVOID* buffer, PUINT size) noexcept { return ::VerQueryValueW(block, sub_block, buffer, size); }
LONG NativeWin32Api::WinVerifyTrust(HWND window, GUID* action, WINTRUST_DATA* data) noexcept { return ::WinVerifyTrust(window, action, data); }
int NativeWin32Api::CompareStringOrdinal(LPCWCH left, int left_length, LPCWCH right, int right_length, BOOL ignore_case) const noexcept { return ::CompareStringOrdinal(left, left_length, right, right_length, ignore_case); }
