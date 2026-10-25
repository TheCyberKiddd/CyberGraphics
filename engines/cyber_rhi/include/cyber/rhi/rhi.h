#pragma once

// Umbrella include for the CyberGraphics RHI. A caller that wants the whole
// interface includes this; a caller that wants one piece includes that header.
//
// The RHI is the only place a graphics API is spoken. Backends live behind
// `backend.h`; nothing in this directory includes a D3D12, Vulkan, or Metal
// header.

#include <cyber/rhi/backend.h>
#include <cyber/rhi/capabilities.h>
#include <cyber/rhi/conventions.h>
#include <cyber/rhi/descriptors.h>
#include <cyber/rhi/device.h>
#include <cyber/rhi/format.h>
#include <cyber/rhi/handle.h>
