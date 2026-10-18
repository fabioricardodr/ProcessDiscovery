#pragma once
#include "pch.h"
#include "ProcessTypes.h"

QueryResult QueryProcessByPid(std::uint32_t pid, const InspectionOptions& options);
QueryResult QueryProcessByName(std::wstring name, const InspectionOptions& options);
QueryResult QueryProcessByFullPath(std::wstring full_path, const InspectionOptions& options);
QueryResult QueryProcess(const InspectionOptions& options);