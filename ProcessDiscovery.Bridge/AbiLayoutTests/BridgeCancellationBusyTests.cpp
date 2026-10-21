#include "../include/process_discovery_bridge.h"
#include <cassert>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <string>
#include <thread>
#include <vector>

static pd_query_options_v1 options(pd_cancellation_handle cancellation) {
	pd_query_options_v1 value{};
	value.struct_size = sizeof(value);
	value.abi_version = PD_ABI_VERSION_1;
	value.include_image_path = 1;
	value.cancellation = cancellation;
	return value;
}

static pd_catalog_handle create_catalog() {
	pd_catalog_handle catalog = nullptr;
	pd_error_v1 error{};
	char operation[128]{};
	char message[256]{};
	error.struct_size = sizeof(error);
	error.operation_utf8 = operation;
	error.operation_capacity = sizeof(operation);
	error.message_utf8 = message;
	error.message_capacity = sizeof(message);
	assert(pd_catalog_create_v1(PD_ABI_VERSION_1, &catalog, &error) == PD_OK);
	return catalog;
}

int main() {
	std::setvbuf(stderr, nullptr, _IONBF, 0);
	std::fprintf(stderr, "start\n");
	pd_catalog_handle catalog = create_catalog();
	std::fprintf(stderr, "catalog_created\n");
	pd_cancellation_handle cancellation = nullptr;
	if (pd_cancellation_create_v1(&cancellation) != PD_OK) return 10;

	pd_cancellation_request_v1(cancellation);
	pd_result_handle cancelled_result = nullptr;
	auto cancelled_options = options(cancellation);
	const pd_status cancelled_status = pd_query_enumerate_v1(catalog, &cancelled_options, &cancelled_result);
	if (cancelled_status != PD_CANCELLED || cancelled_result == nullptr) return 20;
	uint64_t cancelled_size = 0;
	if (pd_result_json_size_v1(cancelled_result, &cancelled_size) != PD_OK) return 21;
	std::vector<char> cancelled_json(static_cast<size_t>(cancelled_size));
	uint64_t cancelled_written = 0;
	if (pd_result_copy_json_v1(cancelled_result, cancelled_json.data(), cancelled_size, &cancelled_written) != PD_OK ||
		std::strstr(cancelled_json.data(), "\"state\":\"partial\"") == nullptr) return 22;
	pd_result_free_v1(cancelled_result);
	std::fprintf(stderr, "pre_cancel_passed\n");

	pd_cancellation_free_v1(cancellation);
	cancellation = nullptr;
	if (pd_cancellation_create_v1(&cancellation) != PD_OK) return 23;

	pd_result_handle result = nullptr;
	auto query_options = options(cancellation);
	pd_testing_block_query_v1(catalog);
	pd_status query_status = PD_INVALID_ARGUMENT;
	std::thread worker([&] { query_status = pd_query_enumerate_v1(catalog, &query_options, &result); });
	std::fprintf(stderr, "worker_started\n");

	if (pd_testing_wait_query_blocked_v1(catalog, 5000) != PD_OK) return 30;
	pd_status free_status = pd_catalog_free_v1(catalog);
	std::fprintf(stderr, "free_status=%u\n", free_status);
	if (free_status != PD_BUSY) return 11;
	pd_cancellation_request_v1(cancellation);
	pd_testing_release_query_v1(catalog);
	worker.join();
	std::fprintf(stderr, "worker_finished status=%u result=%p\n", query_status, static_cast<void*>(result));

	if (query_status != PD_CANCELLED && query_status != PD_OK && query_status != PD_RESULT_FAILED) return 12;
	if (result == nullptr) return 13;
	uint64_t required = 0;
	if (pd_result_json_size_v1(result, &required) != PD_OK) return 14;
	std::vector<char> buffer(static_cast<size_t>(required));
	uint64_t written = 0;
	if (pd_result_copy_json_v1(result, buffer.data(), required, &written) != PD_OK) return 15;
	if (std::strstr(buffer.data(), "\"schema_version\":3") == nullptr) return 16;
	if (query_status == PD_CANCELLED && std::strstr(buffer.data(), "\"state\":\"partial\"") == nullptr) return 17;
	pd_result_free_v1(result);

	if (pd_cancellation_free_v1(cancellation) != PD_OK) return 18;
	if (pd_catalog_free_v1(catalog) != PD_OK) return 19;
	std::puts("cancellation/busy passed");
	return 0;
}
