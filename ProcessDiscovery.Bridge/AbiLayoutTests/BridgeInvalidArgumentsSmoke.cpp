#include "../include/process_discovery_bridge.h"
#include <cassert>
#include <cstdint>

int main() {
	pd_catalog_handle catalog = nullptr;
	pd_result_handle result = nullptr;
	uint64_t size = 0;
	uint64_t written = 0;

	assert(pd_catalog_create_v1(0, &catalog, nullptr) == PD_INVALID_ARGUMENT);
	assert(pd_catalog_create_v1(PD_ABI_VERSION_1, nullptr, nullptr) == PD_INVALID_ARGUMENT);
	assert(pd_query_enumerate_v1(nullptr, nullptr, &result) == PD_INVALID_ARGUMENT);
	assert(pd_query_by_pid_v1(nullptr, 1, nullptr, &result) == PD_INVALID_ARGUMENT);
	assert(pd_result_json_size_v1(nullptr, &size) == PD_INVALID_ARGUMENT);
	assert(pd_result_json_size_v1(nullptr, nullptr) == PD_INVALID_ARGUMENT);
	assert(pd_result_copy_json_v1(nullptr, nullptr, 0, &written) == PD_INVALID_ARGUMENT);
	assert(pd_result_copy_json_v1(nullptr, nullptr, 0, nullptr) == PD_INVALID_ARGUMENT);
	assert(pd_catalog_free_v1(nullptr) == PD_OK);
	assert(pd_cancellation_free_v1(nullptr) == PD_OK);
	pd_result_free_v1(nullptr);
	pd_cancellation_request_v1(nullptr);
	return 0;
}
