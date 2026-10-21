#include "../include/process_discovery_bridge.h"

#include <cassert>
#include <cstdint>
#include <cstring>
#include <vector>
#include <cstdio>

int main() {
	pd_catalog_handle catalog = nullptr;
	pd_error_v1 error{};
	error.struct_size = sizeof(error);
	char operation[128]{};
	char message[256]{};
	error.operation_utf8 = operation;
	error.operation_capacity = sizeof(operation);
	error.message_utf8 = message;
	error.message_capacity = sizeof(message);
	const pd_status create_status = pd_catalog_create_v1(PD_ABI_VERSION_1, &catalog, &error);
	if (create_status != PD_OK) {
		std::fprintf(stderr, "create status=%u operation=%s message=%s\n", create_status, operation, message);
	}
	assert(create_status == PD_OK);
	std::puts("created");

	pd_query_options_v1 options{};
	options.struct_size = sizeof(options);
	options.abi_version = PD_ABI_VERSION_1;
	options.include_image_path = 1;

	pd_result_handle result = nullptr;
	const pd_status status = pd_query_enumerate_v1(catalog, &options, &result);
	assert(status == PD_OK || status == PD_RESULT_FAILED || status == PD_CANCELLED);
	assert(result != nullptr);
	std::puts("queried");

	assert(pd_catalog_free_v1(catalog) == PD_OK);
	std::puts("catalog freed");

	uint64_t required = 0;
	assert(pd_result_json_size_v1(result, &required) == PD_OK);
	std::puts("sized");
	assert(required > 1);
	std::vector<char> buffer(static_cast<size_t>(required));
	uint64_t written = 0;
	assert(pd_result_copy_json_v1(result, buffer.data(), required - static_cast<uint64_t>(1), &written) == PD_BUFFER_TOO_SMALL);
	assert(written == required);
	assert(pd_result_copy_json_v1(result, buffer.data(), required, &written) == PD_OK);
	std::puts("copied");
	assert(buffer.back() == '\0');
	assert(std::strstr(buffer.data(), "\"schema_version\":3") != nullptr);

	pd_result_free_v1(result);
	std::puts("result freed");
	return 0;
}
