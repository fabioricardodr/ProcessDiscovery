#include "BridgeLoader.h"
#include "../third_party/nlohmann/json.hpp"

#include <bcrypt.h>
#define DELAYIMP_INSECURE_WRITABLE_HOOKS
#include <delayimp.h>
#include <winver.h>

#include <array>
#include <cstdio>
#include <filesystem>
#include <mutex>
#include <string>
#include <fstream>
#include <algorithm>
#include <vector>

#pragma comment(lib, "bcrypt.lib")
#pragma comment(lib, "Version.lib")

namespace {

using json = nlohmann::json;

std::once_flag g_load_once;
HMODULE g_native_module = nullptr;
std::string g_error;

constexpr char kNativeVersion[] = "1.0.0.0";
#if defined(_WIN64)
constexpr char kNativeSha256[] = "7d8c3216469e77232fcfa17b2288eb6071b5146b0bdcc1e522e011c656db76ad";
#else
constexpr char kNativeSha256[] = "c7019bc31b2e0fdf2c19927484059efd676380c930913a93dace35d1d37898e3";
#endif

void SetError(const char* value) {
	g_error = value;
}

std::wstring ModuleDirectory() {
	HMODULE self = nullptr;
	if (!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
								GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
							reinterpret_cast<LPCWSTR>(&ModuleDirectory), &self)) {
		return {};
	}
	std::array<wchar_t, MAX_PATH> buffer{};
	const DWORD length = GetModuleFileNameW(self, buffer.data(), static_cast<DWORD>(buffer.size()));
	if (length == 0 || length == buffer.size()) return {};
	return std::filesystem::path(std::wstring(buffer.data(), length)).parent_path().wstring();
}

bool HasReparsePointInPath(const std::filesystem::path& path) {
	std::filesystem::path current = path.root_path();
	for (const auto& component : path.relative_path()) {
		current /= component;
		const DWORD attributes = GetFileAttributesW(current.c_str());
		if (attributes == INVALID_FILE_ATTRIBUTES) return false;
		if ((attributes & FILE_ATTRIBUTE_REPARSE_POINT) != 0) return true;
	}
	return false;
}

bool ReadVersion(const std::filesystem::path& path, const std::string& expected) {
	DWORD ignored = 0;
	const DWORD size = GetFileVersionInfoSizeW(path.c_str(), &ignored);
	if (size == 0) return false;
	std::vector<BYTE> data(size);
	if (!GetFileVersionInfoW(path.c_str(), 0, size, data.data())) return false;
	VS_FIXEDFILEINFO* info = nullptr;
	UINT length = 0;
	if (!VerQueryValueW(data.data(), L"\\", reinterpret_cast<LPVOID*>(&info), &length) ||
		info == nullptr || length < sizeof(VS_FIXEDFILEINFO)) return false;
	char version[32]{};
	sprintf_s(version, "%u.%u.%u.%u", HIWORD(info->dwFileVersionMS),
			  LOWORD(info->dwFileVersionMS), HIWORD(info->dwFileVersionLS),
			  LOWORD(info->dwFileVersionLS));
	return std::string(version) == expected;
}

bool ReadSha256(const std::filesystem::path& path, std::string& result) {
	HANDLE file = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ,
							  nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
	if (file == INVALID_HANDLE_VALUE) return false;
	BCRYPT_ALG_HANDLE algorithm = nullptr;
	BCRYPT_HASH_HANDLE hash = nullptr;
	DWORD object_size = 0;
	DWORD result_size = 0;
	bool ok = BCryptOpenAlgorithmProvider(&algorithm, BCRYPT_SHA256_ALGORITHM, nullptr, 0) == 0 &&
			  BCryptGetProperty(algorithm, BCRYPT_OBJECT_LENGTH, reinterpret_cast<PUCHAR>(&object_size), sizeof(object_size), &result_size, 0) == 0;
	std::vector<UCHAR> object(object_size);
	std::array<UCHAR, 32> digest{};
	if (ok) ok = BCryptCreateHash(algorithm, &hash, object.data(), static_cast<ULONG>(object.size()), nullptr, 0, 0) == 0;
	std::array<BYTE, 64 * 1024> buffer{};
	while (ok) {
		DWORD read = 0;
		if (!ReadFile(file, buffer.data(), static_cast<DWORD>(buffer.size()), &read, nullptr)) { ok = false; break; }
		if (read == 0) break;
		if (BCryptHashData(hash, buffer.data(), read, 0) != 0) { ok = false; break; }
	}

	if (ok) ok = BCryptFinishHash(hash, digest.data(), static_cast<ULONG>(digest.size()), 0) == 0;
	if (hash != nullptr) BCryptDestroyHash(hash);
	if (algorithm != nullptr) BCryptCloseAlgorithmProvider(algorithm, 0);
	CloseHandle(file);
	if (!ok) return false;
	static constexpr char digits[] = "0123456789abcdef";
	result.clear();
	result.reserve(64);
	for (const auto byte : digest) {
		result.push_back(digits[byte >> 4]);
		result.push_back(digits[byte & 0x0f]);
	}
	return true;
}

bool IsExpectedArchitecture(const std::filesystem::path& path) {
	HANDLE file = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, 0, nullptr);
	if (file == INVALID_HANDLE_VALUE) return false;
	HANDLE mapping = CreateFileMappingW(file, nullptr, PAGE_READONLY, 0, 0, nullptr);
	if (mapping == nullptr) { CloseHandle(file); return false; }
	const auto* base = static_cast<const BYTE*>(MapViewOfFile(mapping, FILE_MAP_READ, 0, 0, 0));
	bool valid = false;
	if (base != nullptr) {
		const auto* dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(base);
		if (dos->e_magic == IMAGE_DOS_SIGNATURE) {
			const auto* signature = reinterpret_cast<const DWORD*>(base + dos->e_lfanew);
			if (*signature == IMAGE_NT_SIGNATURE) {
				const auto* file_header = reinterpret_cast<const IMAGE_FILE_HEADER*>(signature + 1);
#if defined(_WIN64)
				valid = file_header->Machine == IMAGE_FILE_MACHINE_AMD64;
#else
				valid = file_header->Machine == IMAGE_FILE_MACHINE_I386;
#endif
			}
		}
	}
	if (base != nullptr) UnmapViewOfFile(base);
	CloseHandle(mapping);
	CloseHandle(file);
	return valid;
}

std::string ActiveArchitecture() {
#if defined(_WIN64)
	return "x64";
#else
	return "x86";
#endif
}

bool IsHexSha256(const std::string& value) {
	return value.size() == 64 && std::all_of(value.begin(), value.end(), [](unsigned char c) {
		return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F');
	});
}

bool ValidateBridgeManifest(const std::filesystem::path& bridge_path) {
	std::ifstream stream(bridge_path.parent_path() / L"artifacts-manifest.json", std::ios::binary);
	if (!stream) { SetError("bridge manifest is missing"); return false; }
	json manifest;
	try { stream >> manifest; } catch (...) { SetError("bridge manifest is invalid JSON"); return false; }
	if (!manifest.is_object() || manifest.size() != 2 || manifest.value("manifest_version", 0) != 1 ||
		!manifest.contains("artifacts") || !manifest["artifacts"].is_array()) {
		SetError("bridge manifest structure is invalid"); return false;
	}
	json selected;
	for (const auto& artifact : manifest["artifacts"]) {
		if (!artifact.is_object()) { SetError("bridge manifest artifact is invalid"); return false; }
		for (const auto& item : artifact.items()) {
			if (item.key() != "artifact_id" && item.key() != "architecture" && item.key() != "relative_path" &&
				item.key() != "file_version" && item.key() != "hash_algorithm" && item.key() != "sha256") {
				SetError("bridge manifest contains an unknown property"); return false;
			}
		}
		if (artifact.value("artifact_id", "") == "bridge" && artifact.value("architecture", "") == ActiveArchitecture()) {
			if (!selected.is_null()) { SetError("active bridge manifest entry is duplicated"); return false; }
			selected = artifact;
		}
	}
	if (selected.is_null() || selected.size() != 6 || !selected.contains("artifact_id") ||
		!selected.contains("architecture") || !selected.contains("relative_path") ||
		!selected.contains("file_version") || !selected.contains("hash_algorithm") ||
		!selected.contains("sha256") || selected.value("hash_algorithm", "") != "SHA-256" ||
		!selected["relative_path"].is_string() || !selected["file_version"].is_string() ||
		!selected["sha256"].is_string() || !IsHexSha256(selected["sha256"])) {
		SetError("active bridge manifest entry is invalid"); return false;
	}
	const auto relative = std::filesystem::path(selected["relative_path"].get<std::string>());
	for (const auto& item : relative) if (item == L"..") { SetError("bridge manifest path escapes package root"); return false; }
	if (std::filesystem::absolute(bridge_path.parent_path() / relative).lexically_normal() != bridge_path ||
		HasReparsePointInPath(bridge_path)) { SetError("bridge manifest path is invalid"); return false; }
	if (!ReadVersion(bridge_path, selected["file_version"])) { SetError("bridge VERSIONINFO differs from manifest"); return false; }
	std::string observed;
	if (!ReadSha256(bridge_path, observed) || _stricmp(observed.c_str(), selected["sha256"].get<std::string>().c_str()) != 0) {
		SetError("bridge SHA-256 differs from manifest"); return false;
	}
	return true;
}

bool ValidateAndLoad() {
	const auto directory = ModuleDirectory();
	if (directory.empty()) { SetError("bridge module path unavailable"); return false; }
	const auto native = std::filesystem::path(directory) / L"ProcessDiscovery.dll";
	const auto bridge = std::filesystem::path(directory) / L"ProcessDiscovery.Bridge.dll";
	std::error_code error;
	const auto absolute = std::filesystem::absolute(native, error).lexically_normal();
	if (error || absolute.parent_path() != std::filesystem::path(directory) || HasReparsePointInPath(absolute)) {
		SetError("native path is outside the private directory or uses a reparse point");
		return false;
	}
	if (!ValidateBridgeManifest(bridge)) return false;
	const DWORD attributes = GetFileAttributesW(absolute.c_str());
	if (attributes == INVALID_FILE_ATTRIBUTES || (attributes & FILE_ATTRIBUTE_DIRECTORY) != 0) {
		SetError("native DLL is missing");
		return false;
	}
	if (!IsExpectedArchitecture(absolute)) { SetError("native architecture is invalid or incompatible"); return false; }
	if (!ReadVersion(absolute, kNativeVersion)) { SetError("native VERSIONINFO is absent or divergent"); return false; }
	std::string observed;
	if (!ReadSha256(absolute, observed) || observed != kNativeSha256) {
		SetError("native SHA-256 is absent or divergent");
		return false;
	}
	g_native_module = LoadLibraryExW(absolute.c_str(), nullptr,
									 LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR | LOAD_LIBRARY_SEARCH_SYSTEM32);
	if (g_native_module == nullptr) { SetError("native LoadLibraryEx failed"); return false; }
	return true;
}

FARPROC WINAPI DelayLoadHook(unsigned notification, PDelayLoadInfo info) {
	if (notification == dliNotePreLoadLibrary && info != nullptr && info->szDll != nullptr &&
		_stricmp(info->szDll, "ProcessDiscovery.dll") == 0) {
		return reinterpret_cast<FARPROC>(g_native_module);
	}
	return nullptr;
}

}

extern "C" PfnDliHook __pfnDliNotifyHook2 = DelayLoadHook;

namespace process_discovery_bridge {

bool EnsureNativeLoaded() noexcept {
	std::call_once(g_load_once, [] { ValidateAndLoad(); });
	return g_native_module != nullptr;
}

const char* LastLoaderError() noexcept {
	return g_error.c_str();
}

}
