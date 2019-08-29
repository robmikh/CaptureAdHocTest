#pragma once

#include <Unknwn.h>
#include <inspectable.h>

#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.Foundation.Collections.h>
#include <winrt/Windows.Foundation.Metadata.h>
#include <winrt/Windows.Storage.h>
#include <winrt/Windows.Storage.Streams.h>
#include <winrt/Windows.System.h>
#include <winrt/Windows.UI.h>
#include <winrt/Windows.UI.Composition.h>
#include <winrt/Windows.UI.Composition.Core.h>
#include <winrt/Windows.Graphics.Capture.h>
#include <winrt/Windows.Graphics.DirectX.h>
#include <winrt/Windows.Graphics.DirectX.Direct3d11.h>

// STL
#include <atomic>
#include <memory>
#include <filesystem>
#include <sstream>
#include <chrono>

// D3D
#include <d3d11_4.h>
#include <dxgi1_6.h>
#include <d2d1_3.h>
#include <wincodec.h>

// Helpers
#include "d3dHelpers.h"
#include "direct3d11.interop.h"
#include "capture.interop.h"
#include "stream.interop.h"
#include "completionSource.h"
#include "safe_flag.h"
#include "FrameTimer.h"
