#pragma once

#include <Windows.h>

namespace process_discovery_bridge {

bool EnsureNativeLoaded() noexcept;
const char* LastLoaderError() noexcept;

}
