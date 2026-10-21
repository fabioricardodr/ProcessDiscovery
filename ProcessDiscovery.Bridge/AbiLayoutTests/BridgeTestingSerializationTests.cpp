#include "../include/process_discovery_bridge.h"
#include <cassert>
#include <cstdio>

int main() {
	pd_catalog_handle catalog = nullptr;
	pd_error_v1 error{};
	char operation[128]{};
	char message[256]{};
	error.struct_size = sizeof(error);
	error.operation_utf8 = operation;
	error.operation_capacity = sizeof(operation);
	error.message_utf8 = message;
	error.message_capacity = sizeof(message);
	if (pd_catalog_create_v1(PD_ABI_VERSION_1, &catalog, &error) != PD_OK) return 10;

	pd_testing_fail_next_json_v1(catalog);
	pd_query_options_v1 options{};
	options.struct_size = sizeof(options);
	options.abi_version = PD_ABI_VERSION_1;
	pd_result_handle result = nullptr;
	const pd_status status = pd_query_by_pid_v1(catalog, 0, &options, &result);
	std::printf("status=%u result=%p\n", status, static_cast<void*>(result));
	if (status != PD_SERIALIZATION_FAILED || result != nullptr) return 11;
	if (pd_catalog_free_v1(catalog) != PD_OK) return 12;
	std::puts("testing serialization passed");
	return 0;
}
