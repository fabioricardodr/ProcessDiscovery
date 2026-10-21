#include "../include/process_discovery_bridge.h"

#include <cassert>
#include <cstddef>
#include <cstdio>

int main() {
	assert(sizeof(pd_error_v1) == (sizeof(void*) == 8 ? 48u : 36u));
	assert(sizeof(pd_query_options_v1) == (sizeof(void*) == 8 ? 24u : 20u));
	assert(offsetof(pd_error_v1, operation_utf8) == (sizeof(void*) == 8 ? 16u : 12u));
	assert(offsetof(pd_error_v1, message_utf8) == (sizeof(void*) == 8 ? 32u : 24u));
	assert(offsetof(pd_query_options_v1, cancellation) == 16u);

	std::printf("pointer_bits=%zu pd_error_v1=%zu pd_query_options_v1=%zu "
				"operation_offset=%zu message_offset=%zu cancellation_offset=%zu\n",
				sizeof(void*) * 8,
				sizeof(pd_error_v1),
				sizeof(pd_query_options_v1),
				offsetof(pd_error_v1, operation_utf8),
				offsetof(pd_error_v1, message_utf8),
				offsetof(pd_query_options_v1, cancellation));
	return 0;
}
