#pragma once

#include "framework.h"
#include <TlHelp32.h>
#include <Softpub.h>
#include <wintrust.h>
#include <winver.h>
#include <algorithm>
#include <string>
#include <vector>

class IWin32Api {
public:
	virtual ~IWin32Api() = default;

	virtual DWORD GetLastErrorValue() const noexcept = 0;
	virtual void ClearLastError() noexcept = 0;
	virtual HANDLE CreateToolhelp32Snapshot(DWORD flags, DWORD process_id) noexcept = 0;
	virtual BOOL Process32FirstW(HANDLE snapshot, PROCESSENTRY32W* entry) noexcept = 0;
	virtual BOOL Process32NextW(HANDLE snapshot, PROCESSENTRY32W* entry) noexcept = 0;
	virtual BOOL Module32FirstW(HANDLE snapshot, MODULEENTRY32W* entry) noexcept = 0;
	virtual BOOL Module32NextW(HANDLE snapshot, MODULEENTRY32W* entry) noexcept = 0;
	virtual HANDLE OpenProcess(DWORD desired_access, BOOL inherit_handle, DWORD process_id) noexcept = 0;
	virtual BOOL CloseHandle(HANDLE handle) noexcept = 0;
	virtual DWORD WaitForSingleObject(HANDLE handle, DWORD milliseconds) noexcept = 0;
	virtual BOOL IsWow64Process2(HANDLE process, PUSHORT process_machine, PUSHORT native_machine) noexcept = 0;
	virtual BOOL QueryFullProcessImageNameW(HANDLE process, DWORD flags, LPWSTR buffer, PDWORD size) noexcept = 0;
	virtual BOOL GetProcessTimes(HANDLE process, LPFILETIME creation, LPFILETIME exit, LPFILETIME kernel, LPFILETIME user) noexcept = 0;
	virtual DWORD GetFileVersionInfoSizeW(LPCWSTR path, LPDWORD handle) noexcept = 0;
	virtual BOOL GetFileVersionInfoW(LPCWSTR path, DWORD handle, DWORD size, LPVOID data) noexcept = 0;
	virtual BOOL VerQueryValueW(const LPVOID block, LPCWSTR sub_block, LPVOID* buffer, PUINT size) noexcept = 0;
	virtual LONG WinVerifyTrust(HWND window, GUID* action, WINTRUST_DATA* data) noexcept = 0;
	virtual int CompareStringOrdinal(LPCWCH left, int left_length, LPCWCH right, int right_length, BOOL ignore_case) const noexcept = 0;
};

IWin32Api& GetWin32Api() noexcept;
void SetWin32ApiForTesting(IWin32Api* api) noexcept;

class NativeWin32Api final : public IWin32Api {
public:
	DWORD GetLastErrorValue() const noexcept override;
	void ClearLastError() noexcept override;
	HANDLE CreateToolhelp32Snapshot(DWORD flags, DWORD process_id) noexcept override;
	BOOL Process32FirstW(HANDLE snapshot, PROCESSENTRY32W* entry) noexcept override;
	BOOL Process32NextW(HANDLE snapshot, PROCESSENTRY32W* entry) noexcept override;
	BOOL Module32FirstW(HANDLE snapshot, MODULEENTRY32W* entry) noexcept override;
	BOOL Module32NextW(HANDLE snapshot, MODULEENTRY32W* entry) noexcept override;
	HANDLE OpenProcess(DWORD desired_access, BOOL inherit_handle, DWORD process_id) noexcept override;
	BOOL CloseHandle(HANDLE handle) noexcept override;
	DWORD WaitForSingleObject(HANDLE handle, DWORD milliseconds) noexcept override;
	BOOL IsWow64Process2(HANDLE process, PUSHORT process_machine, PUSHORT native_machine) noexcept override;
	BOOL QueryFullProcessImageNameW(HANDLE process, DWORD flags, LPWSTR buffer, PDWORD size) noexcept override;
	BOOL GetProcessTimes(HANDLE process, LPFILETIME creation, LPFILETIME exit, LPFILETIME kernel, LPFILETIME user) noexcept override;
	DWORD GetFileVersionInfoSizeW(LPCWSTR path, LPDWORD handle) noexcept override;
	BOOL GetFileVersionInfoW(LPCWSTR path, DWORD handle, DWORD size, LPVOID data) noexcept override;
	BOOL VerQueryValueW(const LPVOID block, LPCWSTR sub_block, LPVOID* buffer, PUINT size) noexcept override;
	LONG WinVerifyTrust(HWND window, GUID* action, WINTRUST_DATA* data) noexcept override;
	int CompareStringOrdinal(LPCWCH left, int left_length, LPCWCH right, int right_length, BOOL ignore_case) const noexcept override;
};

class FakeWin32Api final : public IWin32Api {
public:
	DWORD last_error = ERROR_SUCCESS;
	mutable std::vector<std::string> call_order;
	HANDLE snapshot_result = INVALID_HANDLE_VALUE;
	HANDLE process_result = nullptr;
	BOOL close_result = TRUE;
	DWORD wait_result = WAIT_TIMEOUT;
	BOOL wow64_result = TRUE;
	USHORT process_machine = IMAGE_FILE_MACHINE_UNKNOWN;
	USHORT native_machine = IMAGE_FILE_MACHINE_AMD64;
	BOOL snapshot_first_result = FALSE;
	BOOL snapshot_next_result = FALSE;
	PROCESSENTRY32W process_entry{};
	MODULEENTRY32W module_entry{};
	std::wstring image_path;
	BOOL query_image_result = FALSE;
	DWORD query_image_error = ERROR_ACCESS_DENIED;
	DWORD process_next_error = ERROR_NO_MORE_FILES;
	DWORD module_next_error = ERROR_NO_MORE_FILES;
	BOOL process_times_result = FALSE;
	FILETIME creation_time{};
	FILETIME exit_time{};
	FILETIME kernel_time{};
	FILETIME user_time{};
	LONG wintrust_result = ERROR_SUCCESS;
	DWORD wintrust_calls = 0;
	DWORD first_ui_choice = 0;
	DWORD first_revocation_checks = 0;
	DWORD first_prov_flags = 0;
	DWORD first_state_action = 0;
	DWORD last_state_action = 0;

	DWORD GetLastErrorValue() const noexcept override { call_order.push_back("get_last_error"); return last_error; }
	void ClearLastError() noexcept override { last_error = ERROR_SUCCESS; call_order.push_back("clear_last_error"); }
	HANDLE CreateToolhelp32Snapshot(DWORD, DWORD) noexcept override { return snapshot_result; }
	BOOL Process32FirstW(HANDLE, PROCESSENTRY32W* entry) noexcept override {
		if (snapshot_first_result && entry != nullptr) *entry = process_entry;
		return snapshot_first_result;
	}
	BOOL Process32NextW(HANDLE, PROCESSENTRY32W* entry) noexcept override {
		if (snapshot_next_result && entry != nullptr) *entry = process_entry;
		if (!snapshot_next_result) last_error = process_next_error;
		return snapshot_next_result;
	}
	BOOL Module32FirstW(HANDLE, MODULEENTRY32W* entry) noexcept override {
		if (snapshot_first_result && entry != nullptr) *entry = module_entry;
		return snapshot_first_result;
	}
	BOOL Module32NextW(HANDLE, MODULEENTRY32W* entry) noexcept override {
		if (snapshot_next_result && entry != nullptr) *entry = module_entry;
		if (!snapshot_next_result) last_error = module_next_error;
		return snapshot_next_result;
	}
	HANDLE OpenProcess(DWORD, BOOL, DWORD) noexcept override { return process_result; }
	BOOL CloseHandle(HANDLE) noexcept override { return close_result; }
	DWORD WaitForSingleObject(HANDLE, DWORD) noexcept override { return wait_result; }
	BOOL IsWow64Process2(HANDLE, PUSHORT process, PUSHORT native) noexcept override {
		if (process != nullptr) *process = process_machine;
		if (native != nullptr) *native = native_machine;
		return wow64_result;
	}
	BOOL QueryFullProcessImageNameW(HANDLE, DWORD, LPWSTR buffer, PDWORD size) noexcept override {
		if (!query_image_result) {
			last_error = query_image_error;
			return FALSE;
		}
		if (buffer == nullptr || size == nullptr || *size <= image_path.size()) {
			last_error = ERROR_INSUFFICIENT_BUFFER;
			return FALSE;
		}
		std::copy(image_path.begin(), image_path.end(), buffer);
		*size = static_cast<DWORD>(image_path.size());
		return TRUE;
	}
	BOOL GetProcessTimes(HANDLE, LPFILETIME creation, LPFILETIME exit, LPFILETIME kernel, LPFILETIME user) noexcept override {
		if (!process_times_result) return FALSE;
		if (creation != nullptr) *creation = creation_time;
		if (exit != nullptr) *exit = exit_time;
		if (kernel != nullptr) *kernel = kernel_time;
		if (user != nullptr) *user = user_time;
		return TRUE;
	}
	DWORD GetFileVersionInfoSizeW(LPCWSTR, LPDWORD) noexcept override { return 0; }
	BOOL GetFileVersionInfoW(LPCWSTR, DWORD, DWORD, LPVOID) noexcept override { return FALSE; }
	BOOL VerQueryValueW(const LPVOID, LPCWSTR, LPVOID*, PUINT) noexcept override { return FALSE; }
	LONG WinVerifyTrust(HWND, GUID*, WINTRUST_DATA* data) noexcept override {
		call_order.push_back("WinVerifyTrust");
		if (wintrust_calls == 0 && data != nullptr) {
			first_ui_choice = data->dwUIChoice;
			first_revocation_checks = data->fdwRevocationChecks;
			first_prov_flags = data->dwProvFlags;
			first_state_action = data->dwStateAction;
		}
		if (data != nullptr) last_state_action = data->dwStateAction;
		++wintrust_calls;
		return wintrust_result;
	}
	int CompareStringOrdinal(LPCWCH left, int left_length, LPCWCH right, int right_length, BOOL ignore_case) const noexcept override {
		return ::CompareStringOrdinal(left, left_length, right, right_length, ignore_case);
	}
};
