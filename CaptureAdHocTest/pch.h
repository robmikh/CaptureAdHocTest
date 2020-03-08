﻿#pragma once

// Collision from minwindef min/max and std
#define NOMINMAX 

#include <Unknwn.h>
#include <inspectable.h>

#include <wil/cppwinrt.h>

#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.Foundation.Collections.h>
#include <winrt/Windows.Foundation.Metadata.h>
#include <winrt/Windows.Storage.h>
#include <winrt/Windows.Storage.Streams.h>
#include <winrt/Windows.System.h>
#include <winrt/Windows.UI.h>
#include <winrt/Windows.UI.Composition.h>
#include <winrt/Windows.UI.Composition.Core.h>
#include <winrt/Windows.UI.Composition.Desktop.h>
#include <winrt/Windows.Graphics.Capture.h>
#include <winrt/Windows.Graphics.DirectX.h>
#include <winrt/Windows.Graphics.DirectX.Direct3d11.h>

// STL
#include <atomic>
#include <memory>
#include <filesystem>
#include <sstream>
#include <chrono>
#include <string>
#include <iostream>
#include <future>
#include <variant>
#include <functional>

// WIL
#include <wil/resource.h>

// D3D
#include <d3d11_4.h>
#include <dxgi1_6.h>
#include <d2d1_3.h>
#include <wincodec.h>

// Helpers
#include <robmikh.common/composition.desktop.interop.h>
#include <robmikh.common/composition.interop.h>
#include <robmikh.common/d3dHelpers.h>
#include <robmikh.common/d3dHelpers.desktop.h>
#include <robmikh.common/direct3d11.interop.h>
#include <robmikh.common/stream.interop.h>
#include <robmikh.common/hwnd.interop.h>
#include <robmikh.common/dispatcherqueue.desktop.interop.h>
#include <robmikh.common/capture.desktop.interop.h>
#include "completionSource.h"
#include "FrameTimer.h"
