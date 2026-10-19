#pragma once

#include "ProcessTypes.h"
#include <string>

class AuthenticodeVerifier final {
public:
	Field<SignatureStatus> verify(const std::wstring& image_path) const;
};
