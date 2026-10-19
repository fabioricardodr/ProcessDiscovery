#include "pch.h"
#include "AuthenticodeVerifier.h"
#include "Win32Api.h"
#include <Softpub.h>
#include <wintrust.h>

Field<SignatureStatus> AuthenticodeVerifier::verify(const std::wstring& image_path) const {
	if (image_path.empty()) {
		return Field<SignatureStatus>::failure(ErrorInfo{
			"AuthenticodeVerifier", ERROR_INVALID_PARAMETER,
			"O caminho da imagem não pode ser vazio.", ErrorDomain::validation
		});
	}

	SignatureStatus status{
		SignaturePresence::unknown,
		TrustState::indeterminate,
		0,
		std::nullopt,
		L"WTD_UI_NONE;WTD_REVOKE_NONE;WTD_CACHE_ONLY_URL_RETRIEVAL"
	};
	WINTRUST_FILE_INFO file_info{};
	file_info.cbStruct = sizeof(file_info);
	file_info.pcwszFilePath = image_path.c_str();
	WINTRUST_DATA trust_data{};
	trust_data.cbStruct = sizeof(trust_data);
	trust_data.dwUIChoice = WTD_UI_NONE;
	trust_data.fdwRevocationChecks = WTD_REVOKE_NONE;
	trust_data.dwUnionChoice = WTD_CHOICE_FILE;
	trust_data.pFile = &file_info;
	trust_data.dwStateAction = WTD_STATEACTION_VERIFY;
	trust_data.dwProvFlags = WTD_CACHE_ONLY_URL_RETRIEVAL;

	const GUID action = WINTRUST_ACTION_GENERIC_VERIFY_V2;
	const LONG verify_status = GetWin32Api().WinVerifyTrust(nullptr, const_cast<GUID*>(&action), &trust_data);
	status.wintrust_status = verify_status;
	status.last_error = GetWin32Api().GetLastErrorValue();

	trust_data.dwStateAction = WTD_STATEACTION_CLOSE;
	GetWin32Api().WinVerifyTrust(nullptr, const_cast<GUID*>(&action), &trust_data);

	if (verify_status == ERROR_SUCCESS) {
		status.presence = SignaturePresence::present;
		status.trust = TrustState::valid;
	} else if (verify_status == TRUST_E_NOSIGNATURE) {
		status.presence = SignaturePresence::no_embedded_signature;
		status.trust = TrustState::not_evaluated;
	} else {
		status.trust = TrustState::invalid;
	}
	return Field<SignatureStatus>::available(std::move(status));
}
