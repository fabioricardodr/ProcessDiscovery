#include "pch.h"
#include "VersionResourceReader.h"
#include "Win32Api.h"
#include <vector>

namespace {
struct LanguageAndCodePage {
	WORD language;
	WORD code_page;
};
}

Field<std::wstring> VersionResourceReader::read(const std::wstring& image_path) const {
	if (image_path.empty()) {
		return Field<std::wstring>::failure(ErrorInfo{
			"VersionResourceReader", ERROR_INVALID_PARAMETER,
			"O caminho da imagem não pode ser vazio.", ErrorDomain::validation
		});
	}

	DWORD reserved_handle = 0;
	const DWORD size = GetWin32Api().GetFileVersionInfoSizeW(image_path.c_str(), &reserved_handle);
	if (size == 0) {
		const DWORD error = GetWin32Api().GetLastErrorValue();
		const FieldState state = error == ERROR_ACCESS_DENIED
			? FieldState::access_denied : FieldState::unavailable;
		const ErrorInfo diagnostic{
			"GetFileVersionInfoSizeW", error,
			"O recurso VERSIONINFO não está disponível ou não pôde ser acessado.",
			ErrorDomain::version_resource
		};
		return state == FieldState::access_denied
			? Field<std::wstring>::access_denied(diagnostic)
			: Field<std::wstring>::unavailable(diagnostic);
	}

	std::vector<std::byte> version_data(size);
	if (!GetWin32Api().GetFileVersionInfoW(image_path.c_str(), 0, size, version_data.data())) {
		const DWORD error = GetWin32Api().GetLastErrorValue();
		const ErrorInfo diagnostic{
			"GetFileVersionInfoW", error,
			"Não foi possível carregar o recurso VERSIONINFO.",
			ErrorDomain::version_resource
		};
		return error == ERROR_ACCESS_DENIED
			? Field<std::wstring>::access_denied(diagnostic)
			: Field<std::wstring>::unavailable(diagnostic);
	}

	void* translation_data = nullptr;
	UINT translation_size = 0;
	if (!GetWin32Api().VerQueryValueW(
			version_data.data(), L"\\VarFileInfo\\Translation",
			&translation_data, &translation_size) ||
		translation_data == nullptr || translation_size < sizeof(LanguageAndCodePage)) {
		return Field<std::wstring>::unavailable(ErrorInfo{
			"VerQueryValueW", ERROR_RESOURCE_DATA_NOT_FOUND,
			"O recurso não possui uma tabela de tradução de versão.",
			ErrorDomain::version_resource
		});
	}

	const auto* translations = static_cast<const LanguageAndCodePage*>(translation_data);
	const std::size_t translation_count = translation_size / sizeof(LanguageAndCodePage);
	for (std::size_t index = 0; index < translation_count; ++index) {
		wchar_t sub_block[64]{};
		swprintf_s(sub_block, L"\\StringFileInfo\\%04x%04x\\InternalName",
			translations[index].language, translations[index].code_page);

		void* value_data = nullptr;
		UINT value_size = 0;
		if (GetWin32Api().VerQueryValueW(version_data.data(), sub_block, &value_data, &value_size) &&
			value_data != nullptr && value_size > 1) {
			std::wstring internal_name(static_cast<const wchar_t*>(value_data), value_size - 1);
			while (!internal_name.empty() && internal_name.back() == L'\0') internal_name.pop_back();
			return Field<std::wstring>::available(std::move(internal_name));
		}
	}

	return Field<std::wstring>::unavailable(ErrorInfo{
		"VerQueryValueW", ERROR_RESOURCE_NAME_NOT_FOUND,
		"O campo InternalName não está presente no recurso de versão.",
		ErrorDomain::version_resource
	});
}
