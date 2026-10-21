#ifndef PROCESS_DISCOVERY_BRIDGE_H
#define PROCESS_DISCOVERY_BRIDGE_H

#include <stdint.h>
#include <stddef.h>

#if defined(_WIN32)
#if defined(PD_BRIDGE_BUILD)
#define PD_API __declspec(dllexport)
#else
#define PD_API __declspec(dllimport)
#endif
#define PD_CALL __cdecl
#else
#define PD_API
#define PD_CALL
#endif

#define PD_PACK 8

#ifdef __cplusplus
extern "C" {
#endif

typedef uint32_t pd_abi_version;
typedef uint32_t pd_status;

#define PD_ABI_VERSION_1       1u
#define PD_SCHEMA_VERSION_3    3u
#define PD_OK                    0u
#define PD_INVALID_ARGUMENT      1u
#define PD_ABI_MISMATCH          2u
#define PD_LOAD_FAILED           3u
#define PD_NATIVE_EXCEPTION      4u
#define PD_RESULT_FAILED         5u
#define PD_CANCELLED             6u
#define PD_BUFFER_TOO_SMALL      7u
#define PD_LIMIT_EXCEEDED        8u
#define PD_NOT_SUPPORTED         9u
#define PD_BUSY                 10u
#define PD_SERIALIZATION_FAILED 11u

typedef struct pd_catalog_handle_t* pd_catalog_handle;
typedef struct pd_result_handle_t* pd_result_handle;
typedef struct pd_cancellation_handle_t* pd_cancellation_handle;

#pragma pack(push, PD_PACK)
typedef struct pd_error_v1 {
	uint32_t struct_size;
	uint32_t domain;
	uint32_t native_code;
	char* operation_utf8;
	uint32_t operation_capacity;
	uint32_t operation_size;
	char* message_utf8;
	uint32_t message_capacity;
	uint32_t message_size;
} pd_error_v1;

typedef struct pd_query_options_v1 {
	uint32_t struct_size;
	uint32_t abi_version;
	uint8_t include_image_path;
	uint8_t include_internal_name;
	uint8_t include_signature;
	uint8_t include_modules;
	uint8_t allow_remote_file_inspection;
	uint8_t reserved[3];
	pd_cancellation_handle cancellation;
} pd_query_options_v1;
#pragma pack(pop)

#define PD_ERROR_V1_MIN_SIZE \
	(offsetof(pd_error_v1, message_size) + sizeof(uint32_t))
#define PD_QUERY_OPTIONS_V1_MIN_SIZE sizeof(pd_query_options_v1)

PD_API pd_status PD_CALL pd_cancellation_create_v1(
	pd_cancellation_handle* cancellation);
PD_API void PD_CALL pd_cancellation_request_v1(
	pd_cancellation_handle cancellation);
PD_API pd_status PD_CALL pd_cancellation_free_v1(
	pd_cancellation_handle cancellation);
PD_API pd_status PD_CALL pd_catalog_create_v1(
	pd_abi_version abi_version,
	pd_catalog_handle* catalog,
	pd_error_v1* error);
PD_API pd_status PD_CALL pd_catalog_free_v1(pd_catalog_handle catalog);
PD_API pd_status PD_CALL pd_query_enumerate_v1(
	pd_catalog_handle catalog,
	const pd_query_options_v1* options,
	pd_result_handle* result);
PD_API pd_status PD_CALL pd_query_by_pid_v1(
	pd_catalog_handle catalog,
	uint32_t pid,
	const pd_query_options_v1* options,
	pd_result_handle* result);
PD_API pd_status PD_CALL pd_result_json_size_v1(
	pd_result_handle result,
	uint64_t* required_bytes);
PD_API pd_status PD_CALL pd_result_copy_json_v1(
	pd_result_handle result,
	char* buffer,
	uint64_t capacity,
	uint64_t* written_bytes);
PD_API void PD_CALL pd_result_free_v1(pd_result_handle result);
PD_API pd_status PD_CALL pd_get_last_error_v1(
	pd_catalog_handle catalog,
	pd_error_v1* error);

#if defined(PD_TESTING)
PD_API void PD_CALL pd_testing_fail_next_json_v1(pd_catalog_handle catalog);
PD_API void PD_CALL pd_testing_block_query_v1(pd_catalog_handle catalog);
PD_API pd_status PD_CALL pd_testing_wait_query_blocked_v1(pd_catalog_handle catalog, uint32_t timeout_ms);
PD_API void PD_CALL pd_testing_release_query_v1(pd_catalog_handle catalog);
#endif

#ifdef __cplusplus
static_assert(sizeof(pd_query_options_v1) ==
				  (sizeof(void*) == 8 ? 24u : 20u),
			  "pd_query_options_v1 layout must remain stable per architecture");
static_assert(sizeof(pd_error_v1) ==
				  (sizeof(void*) == 8 ? 48u : 36u),
			  "pd_error_v1 layout must remain stable per architecture");
static_assert(offsetof(pd_error_v1, struct_size) == 0u, "pd_error_v1.struct_size offset");
static_assert(offsetof(pd_error_v1, domain) == 4u, "pd_error_v1.domain offset");
static_assert(offsetof(pd_error_v1, native_code) == 8u, "pd_error_v1.native_code offset");
static_assert(offsetof(pd_error_v1, operation_utf8) == (sizeof(void*) == 8 ? 16u : 12u), "pd_error_v1.operation_utf8 offset");
static_assert(offsetof(pd_error_v1, message_utf8) == (sizeof(void*) == 8 ? 32u : 24u), "pd_error_v1.message_utf8 offset");
static_assert(offsetof(pd_query_options_v1, cancellation) == (sizeof(void*) == 8 ? 16u : 16u), "pd_query_options_v1.cancellation offset");
#else
_Static_assert(sizeof(pd_query_options_v1) ==
				   (sizeof(void*) == 8 ? 24u : 20u),
			   "pd_query_options_v1 layout must remain stable per architecture");
_Static_assert(sizeof(pd_error_v1) ==
				   (sizeof(void*) == 8 ? 48u : 36u),
			   "pd_error_v1 layout must remain stable per architecture");
_Static_assert(offsetof(pd_error_v1, struct_size) == 0u, "pd_error_v1.struct_size offset");
_Static_assert(offsetof(pd_error_v1, domain) == 4u, "pd_error_v1.domain offset");
_Static_assert(offsetof(pd_error_v1, native_code) == 8u, "pd_error_v1.native_code offset");
_Static_assert(offsetof(pd_error_v1, operation_utf8) == (sizeof(void*) == 8 ? 16u : 12u), "pd_error_v1.operation_utf8 offset");
_Static_assert(offsetof(pd_error_v1, message_utf8) == (sizeof(void*) == 8 ? 32u : 24u), "pd_error_v1.message_utf8 offset");
_Static_assert(offsetof(pd_query_options_v1, cancellation) == (sizeof(void*) == 8 ? 16u : 16u), "pd_query_options_v1.cancellation offset");
#endif

#ifdef __cplusplus
}
#endif

#endif
