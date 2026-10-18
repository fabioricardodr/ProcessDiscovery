#pragma once
#include "framework.h"

class SnapshotHandle
{
public:
	explicit SnapshotHandle(HANDLE handle) : m_handle(handle) {}

	~SnapshotHandle() {
		if (m_handle != INVALID_HANDLE_VALUE) {
			CloseHandle(m_handle);
		}
	}

	SnapshotHandle(const SnapshotHandle&) = delete;
	SnapshotHandle& operator=(const SnapshotHandle&) = delete;

	bool is_valid() const noexcept {
		return m_handle != INVALID_HANDLE_VALUE;
	}

	HANDLE get() const noexcept {
		return m_handle;
	}

private:
	HANDLE m_handle{ INVALID_HANDLE_VALUE };
};

