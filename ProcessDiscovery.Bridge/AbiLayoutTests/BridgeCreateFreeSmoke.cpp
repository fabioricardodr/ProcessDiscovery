#include "../include/process_discovery_bridge.h"
#include <cstdio>

int main() {
	pd_error_v1 error{};
	char operation[128]{};
	char message[256]{};
	error.struct_size = sizeof(error);
	error.operation_utf8 = operation;
	error.operation_capacity = sizeof(operation);
	error.message_utf8 = message;
	error.message_capacity = sizeof(message);

	std::puts("before create");
	pd_catalog_handle catalog = nullptr;
	const pd_status create = pd_catalog_create_v1(PD_ABI_VERSION_1, &catalog, &error);
	std::printf("create=%u catalog=%p operation=%s message=%s\n", create, static_cast<void*>(catalog), operation, message);
	if (create != PD_OK || catalog == nullptr) return 1;

	const pd_status free_status = pd_catalog_free_v1(catalog);
	std::printf("free=%u\n", free_status);
	return free_status == PD_OK ? 0 : 2;
}
