#include "pch.h"
#include "ProcessCatalog.h"
#include "ProcessEnumerator.h"

QueryResult ProcessCatalog::enumerate(const InspectionOptions& options) {
	return QueryProcess(options);
}

QueryResult ProcessCatalog::by_pid(std::uint32_t pid, const InspectionOptions& options) {
	return QueryProcessByPid(pid, options);
}

QueryResult ProcessCatalog::by_name(std::wstring_view name, const InspectionOptions& options) {
	return QueryProcessByName(std::wstring(name), options);
}

QueryResult ProcessCatalog::by_full_path(std::wstring_view path, const InspectionOptions& options) {
	return QueryProcessByFullPath(std::wstring(path), options);
}