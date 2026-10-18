#pragma once
#include "ProcessTypes.h"
#include <string_view>

#ifdef PROCESSDISCOVERY_EXPORTS
#define PROCESSDISCOVERY_API __declspec(dllexport)
#else
#define PROCESSDISCOVERY_API __declspec(dllimport)
#endif

class PROCESSDISCOVERY_API ProcessCatalog {
public:
	QueryResult enumerate(const InspectionOptions& options = {});
	QueryResult by_pid(std::uint32_t pid, const InspectionOptions& options = {});
	QueryResult by_name(std::wstring_view name, const InspectionOptions& options = {});
	QueryResult by_full_path(std::wstring_view path, const InspectionOptions& options = {});
};