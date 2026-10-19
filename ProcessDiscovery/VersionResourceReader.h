#pragma once

#include "ProcessTypes.h"
#include <string>

class VersionResourceReader final {
public:
	Field<std::wstring> read(const std::wstring& image_path) const;
};
