﻿#include "pch.h"
#include "CaptureSnapshot.h"
#include "FullscreenMaxRateWindow.h"
#include "DummyWindow.h"
#include "FullscreenTransitionWindow.h"
#include "wcliparse.h"
#include "testutils.h"
#include "AdHocTestCliParser.h"

using namespace winrt;
using namespace Windows::Foundation;
using namespace Windows::Graphics::Capture;
using namespace Windows::Graphics::DirectX;
using namespace Windows::Graphics::DirectX::Direct3D11;
using namespace Windows::Storage;
using namespace Windows::System;
using namespace Windows::UI;
using namespace Windows::UI::Composition;
using namespace Windows::UI::Composition::Core;

namespace util
{
    using namespace robmikh::common::desktop;
    using namespace robmikh::common::uwp;
}

template <typename T, typename ... Args>
IAsyncOperation<T> CreateOnThreadAsync(DispatcherQueue const& threadQueue, Args ... args)
{
    wil::shared_event initialized(wil::EventOptions::None);
    T thing{ nullptr };
    winrt::check_bool(threadQueue.TryEnqueue([=, &thing, &initialized]()
    {
        thing = T(args...);
        initialized.SetEvent();
    }));
    co_await winrt::resume_on_signal(initialized.get());
    co_return thing;
}

template <typename T, typename ... Args>
std::future<std::shared_ptr<T>> CreateSharedOnThreadAsync(DispatcherQueue const& threadQueue, Args ... args)
{
    wil::shared_event initialized(wil::EventOptions::None);
    std::shared_ptr<T> thing{ nullptr };
    winrt::check_bool(threadQueue.TryEnqueue([=, &thing, &initialized]()
    {
        thing = std::make_shared<T>(args...);
        initialized.SetEvent();
    }));
    co_await winrt::resume_on_signal(initialized.get());
    co_return thing;
}

template<class... Ts> struct overloaded : Ts... { using Ts::operator()...; };
template<class... Ts> overloaded(Ts...)->overloaded<Ts...>;

IAsyncOperation<StorageFile> SaveFrameAsync(IDirect3DDevice const& device, IDirect3DSurface const& surface, std::wstring const& fileName)
{
    // Get a file to save the screenshot
    auto currentPath = std::filesystem::current_path();
    auto folder = co_await StorageFolder::GetFolderFromPathAsync(currentPath.wstring());
    auto file = co_await folder.CreateFileAsync(fileName.c_str(), CreationCollisionOption::ReplaceExisting);

    // Get the file stream
    auto randomAccessStream = co_await file.OpenAsync(FileAccessMode::ReadWrite);
    auto stream = util::CreateStreamFromRandomAccessStream(randomAccessStream);

    // Get the DXGI surface from the frame
    auto dxgiFrameTexture = GetDXGIInterfaceFromObject<IDXGISurface>(surface);

    // Create graphics resources
    auto d3dDevice = GetDXGIInterfaceFromObject<ID3D11Device>(device);

    // Get a D2D bitmap for our snapshot
    // TODO: Since this sample doesn't use D2D any other way, it may be better to map 
    //       the pixels manually and hand them to WIC. However, using d2d is easier for now.
    auto d2dFactory = util::CreateD2DFactory();
    auto d2dDevice = util::CreateD2DDevice(d2dFactory, d3dDevice);
    winrt::com_ptr<ID2D1DeviceContext> d2dContext;
    check_hresult(d2dDevice->CreateDeviceContext(D2D1_DEVICE_CONTEXT_OPTIONS_NONE, d2dContext.put()));
    com_ptr<ID2D1Bitmap1> d2dBitmap;
    check_hresult(d2dContext->CreateBitmapFromDxgiSurface(dxgiFrameTexture.get(), nullptr, d2dBitmap.put()));

    // Encode the snapshot
    auto wicFactory = util::CreateWICFactory();
    com_ptr<IWICBitmapEncoder> encoder;
    check_hresult(wicFactory->CreateEncoder(GUID_ContainerFormatPng, nullptr, encoder.put()));
    check_hresult(encoder->Initialize(stream.get(), WICBitmapEncoderNoCache));

    com_ptr<IWICBitmapFrameEncode> wicFrame;
    com_ptr<IPropertyBag2> frameProperties;
    check_hresult(encoder->CreateNewFrame(wicFrame.put(), frameProperties.put()));
    check_hresult(wicFrame->Initialize(frameProperties.get()));

    com_ptr<IWICImageEncoder> imageEncoder;
    check_hresult(wicFactory->CreateImageEncoder(d2dDevice.get(), imageEncoder.put()));
    check_hresult(imageEncoder->WriteFrame(d2dBitmap.get(), wicFrame.get(), nullptr));
    check_hresult(wicFrame->Commit());
    check_hresult(encoder->Commit());

    co_return file;
}

IAsyncOperation<bool> TransparencyTest(CompositorController const& compositorController, IDirect3DDevice const& device)
{
    auto compositor = compositorController.Compositor();
    auto d3dDevice = GetDXGIInterfaceFromObject<ID3D11Device>(device);
    com_ptr<ID3D11DeviceContext> d3dContext;
    d3dDevice->GetImmediateContext(d3dContext.put());

    IDirect3DSurface frame{ nullptr };
    auto success = true;
    try
    {
        // Build the visual tree
        // A red circle centered in a 100 x 100 bitmap with a transparent background.
        auto visual = compositor.CreateShapeVisual();
        visual.Size({ 100, 100 });
        auto geometry = compositor.CreateEllipseGeometry();
        geometry.Center({ 50, 50 });
        geometry.Radius({ 50, 50 });
        auto shape = compositor.CreateSpriteShape(geometry);
        shape.FillBrush(compositor.CreateColorBrush(Colors::Red()));
        visual.Shapes().Append(shape);

        // Capture the tree
        auto item = GraphicsCaptureItem::CreateFromVisual(visual);
        auto asyncOperation = CaptureSnapshot::TakeAsync(device, item, true); // we want the texture to be a staging texture
        // We need to commit before we wait on this
        compositorController.Commit();
        frame = co_await asyncOperation;
        auto frameTexture = GetDXGIInterfaceFromObject<ID3D11Texture2D>(frame);

        // Map the texture and check the image
        {
            auto mapped = MappedTexture(d3dContext, frameTexture);

            check_color(mapped.ReadBGRAPixel(50, 50), Colors::Red());
            // We don't use Colors::Transparent() here becuase that is transparent white.
            // Right now the capture API uses transparent black to clear.
            check_color(mapped.ReadBGRAPixel(5, 5), Color{ 0, 0, 0, 0 });
        }
    }
    catch (hresult_error const& error)
    {
        wprintf(L"Transparency test failed! 0x%08x - %s \n", error.code().value, error.message().c_str());
        success = false;
    }

    if (!success && frame != nullptr)
    {
        auto file = co_await SaveFrameAsync(device, frame, L"alpha_failure.png");
        wprintf(L"Failure file saved: %s\n", file.Path().c_str());
    }

    co_return success;
}

IAsyncOperation<bool> RenderRateTest(CompositorController const& compositorController, IDirect3DDevice const& device, DispatcherQueue const& compositorThreadQueue, testparams::FullscreenMode mode)
{
    auto compositor = compositorController.Compositor();
    auto d3dDevice = GetDXGIInterfaceFromObject<ID3D11Device>(device);
    com_ptr<ID3D11DeviceContext> d3dContext;
    d3dDevice->GetImmediateContext(d3dContext.put());

    try
    {
        // Create the window on the compositor thread to borrow the message pump
        auto window = co_await CreateSharedOnThreadAsync<FullscreenMaxRateWindow>(compositorThreadQueue, mode);

        // Start capturing the window. Make note of the timestamps.
        auto item = util::CreateCaptureItemForWindow(window->m_window);
        auto framePool = Direct3D11CaptureFramePool::CreateFreeThreaded(
            device,
            DirectXPixelFormat::B8G8R8A8UIntNormalized,
            2,
            item.Size());
        auto session = framePool.CreateCaptureSession(item);
        FrameTimer<TimeSpan> captureTimer;
        framePool.FrameArrived([&captureTimer](auto& framePool, auto&)
        {
            auto frame = framePool.TryGetNextFrame();
            auto timestamp = frame.SystemRelativeTime();

            captureTimer.RecordTimestamp(timestamp);
        });
        session.StartCapture();

        // Run the window
        auto completed = false;
        FrameTimer<std::chrono::time_point<std::chrono::steady_clock>> renderTimer;
        while (!completed)
        {
            if (window->Closed())
            {
                completed = true;
            }

            window->Flip();
            renderTimer.RecordTimestamp(std::chrono::high_resolution_clock::now());
        }

        // The window may already be closed, so don't check the return value
        CloseWindow(window->m_window);
        session.Close();
        framePool.Close();

        auto renderAverageFrameTime = renderTimer.ComputeAverageFrameTime();
        auto captureAverageFrameTime = captureTimer.ComputeAverageFrameTime();

        wprintf(L"Average rendered frame time: %fms\n", renderAverageFrameTime.count());
        wprintf(L"Number of rendered frames: %d\n", renderTimer.m_totalFrames);
        wprintf(L"Average capture frame time: %fms\n", captureAverageFrameTime.count());
        wprintf(L"Number of capture frames: %d\n", captureTimer.m_totalFrames);

        // TODO: Compare average frame times and determine if they are close enough.
    }
    catch (hresult_error const& error)
    {
        wprintf(L"Render rate test failed! 0x%08x - %s \n", error.code().value, error.message().c_str());
        co_return false;
    }

    co_return true;
}

IAsyncOperation<bool> FullscreenTransitionTest(CompositorController const& compositorController, IDirect3DDevice const& device, DispatcherQueue const& compositorThreadQueue, testparams::FullscreenTransitionTestMode mode)
{
    auto compositor = compositorController.Compositor();
    auto d3dDevice = GetDXGIInterfaceFromObject<ID3D11Device>(device);
    com_ptr<ID3D11DeviceContext> d3dContext;
    d3dDevice->GetImmediateContext(d3dContext.put());

    try
    {
        // Create the window on the compositor thread to borrow the message pump
        auto window = co_await CreateSharedOnThreadAsync<FullscreenTransitionWindow>(compositorThreadQueue, mode);
        window->Flip(Colors::Red());

        if (mode == testparams::FullscreenTransitionTestMode::AdHoc)
        {
            co_await winrt::resume_on_signal(window->Closed().get());
        }
        else
        {
            // Start the capture
            auto item = util::CreateCaptureItemForWindow(window->m_window);
            auto framePool = Direct3D11CaptureFramePool::CreateFreeThreaded(
                device,
                DirectXPixelFormat::B8G8R8A8UIntNormalized,
                1,
                item.Size());
            auto session = framePool.CreateCaptureSession(item);
            Direct3D11CaptureFrame currentFrame{ nullptr };
            auto frameEvent = wil::shared_event(wil::EventOptions::None);
            framePool.FrameArrived([&currentFrame, frameEvent](auto& framePool, auto&)
                {
                    WINRT_ASSERT(!currentFrame);
                    currentFrame = framePool.TryGetNextFrame();
                    WINRT_ASSERT(!frameEvent.is_signaled());
                    frameEvent.SetEvent();
                });
            session.StartCapture();
            co_await winrt::resume_on_signal(frameEvent.get());

            // Test for red
            TestCenterOfSurface(device, currentFrame.Surface(), Colors::Red());

            // Transition to fullscreen
            window->Fullscreen(true);
            window->Flip(Colors::Green());
            // Wait for the transition
            co_await std::chrono::milliseconds(500);

            // Release the frame and get a new one
            frameEvent.ResetEvent();
            currentFrame.Close();
            currentFrame = nullptr;
            co_await winrt::resume_on_signal(frameEvent.get());

            // Test for green
            TestCenterOfSurface(device, currentFrame.Surface(), Colors::Green());

            // Transition to windowed
            window->Fullscreen(false);
            window->Flip(Colors::Blue());
            // Wait for the transition
            co_await std::chrono::milliseconds(500);

            // Release the frame and get a new one
            frameEvent.ResetEvent();
            currentFrame.Close();
            currentFrame = nullptr;
            co_await winrt::resume_on_signal(frameEvent.get());

            // Test for blue
            TestCenterOfSurface(device, currentFrame.Surface(), Colors::Blue());
        }
    }
    catch (hresult_error const& error)
    {
        wprintf(L"Fullscreen Transition test failed! 0x%08x - %s \n", error.code().value, error.message().c_str());
        co_return false;
    }

    co_return true;
}

IAsyncOperation<bool> WindowRenderRateTest(
    CompositorController const& compositorController, 
    IDirect3DDevice const& device, 
    std::wstring const& windowName,
    std::chrono::seconds delay,
    std::chrono::seconds duration)
{
    auto compositor = compositorController.Compositor();
    auto d3dDevice = GetDXGIInterfaceFromObject<ID3D11Device>(device);
    com_ptr<ID3D11DeviceContext> d3dContext;
    d3dDevice->GetImmediateContext(d3dContext.put());

    co_await delay;

    try
    {
        // Find the window
        auto window = FindWindowW(nullptr, windowName.c_str());
        winrt::check_bool(window);

        // Start capturing the window. Make note of the timestamps.
        auto item = util::CreateCaptureItemForWindow(window);
        auto framePool = Direct3D11CaptureFramePool::CreateFreeThreaded(
            device,
            DirectXPixelFormat::B8G8R8A8UIntNormalized,
            2,
            item.Size());
        auto session = framePool.CreateCaptureSession(item);
        FrameTimer<TimeSpan> captureTimer;
        framePool.FrameArrived([&captureTimer](auto& framePool, auto&)
        {
            auto frame = framePool.TryGetNextFrame();
            auto timestamp = frame.SystemRelativeTime();

            captureTimer.RecordTimestamp(timestamp);
        });
        session.StartCapture();

        // Run for awhile
        co_await duration;

        session.Close();
        framePool.Close();

        wprintf(L"Average capture frame time: %fms\n", captureTimer.ComputeAverageFrameTime().count());
        wprintf(L"Number of capture frames: %d\n", captureTimer.m_totalFrames);
    }
    catch (hresult_error const& error)
    {
        wprintf(L"Render rate test failed! 0x%08x - %s \n", error.code().value, error.message().c_str());
        co_return false;
    }

    co_return true;
}

auto PrepareWindowAndCursorForCenterTest(HWND window)
{
    // Push the window to the top
    winrt::check_bool(SetWindowPos(window, HWND_TOP, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE));

    // Find where the window is
    RECT rect = {};
    winrt::check_bool(GetWindowRect(window, &rect));
    auto windowWidth = rect.right - rect.left;
    auto windowHeight = rect.bottom - rect.top;

    // Move the cursor to the middle of the window
    auto mouseX = rect.left + windowWidth / 2;
    auto mouseY = rect.top + windowHeight / 2;
    winrt::check_bool(SetCursorPos(mouseX, mouseY));

    return std::pair(mouseX, mouseY);
}

enum class RemoteCaptureType : uint32_t
{
    Window,
    Monitor
};

std::wstring RemoteCaptureTypeToString(RemoteCaptureType captureType)
{
    switch (captureType)
    {
    case RemoteCaptureType::Monitor:
        return L"Monitor";
    case RemoteCaptureType::Window:
        return L"Window";
    }
}

std::future<std::pair<IDirect3DSurface, Color>> TestCenterOfWindowAsync(IDirect3DDevice const& device, HWND window, bool cursorEnabled, RemoteCaptureType captureType)
{
    auto d3dDevice = GetDXGIInterfaceFromObject<ID3D11Device>(device);
    com_ptr<ID3D11DeviceContext> d3dContext;
    d3dDevice->GetImmediateContext(d3dContext.put());

    auto [mouseX, mouseY] = PrepareWindowAndCursorForCenterTest(window);

    GraphicsCaptureItem item{ nullptr };
    switch (captureType)
    {
    case RemoteCaptureType::Monitor:
    {
        // Get the monitor the window is on
        auto monitor = MonitorFromWindow(window, MONITOR_DEFAULTTONULL);
        winrt::check_bool(monitor);
        auto monitorInfo = CreateWin32Struct<MONITORINFO>();
        winrt::check_bool(GetMonitorInfoW(monitor, &monitorInfo));
        mouseX -= monitorInfo.rcMonitor.left;
        mouseY -= monitorInfo.rcMonitor.top;

        item = util::CreateCaptureItemForMonitor(monitor);
    }
    break;
    case RemoteCaptureType::Window:
    {
        // Find where the window is
        RECT rect = {};
        winrt::check_bool(GetWindowRect(window, &rect));
        mouseX -= rect.left;
        mouseY -= rect.top;

        item = util::CreateCaptureItemForWindow(window);
    }
    break;
    }

    auto frame = co_await CaptureSnapshot::TakeAsync(device, item, true, cursorEnabled);
    auto frameTexture = GetDXGIInterfaceFromObject<ID3D11Texture2D>(frame);

    // Map the texture and check the image
    auto mapped = MappedTexture(d3dContext, frameTexture);
    co_return std::pair<IDirect3DSurface, Color>(frame, mapped.ReadBGRAPixel(mouseX, mouseY).to_color());
}

IAsyncOperation<bool> TestCenterOfWindowAsync(RemoteCaptureType captureType, IDirect3DDevice const& device, HWND window, Color windowColor, Color cursorColor)
{
    auto cursorEnabled = true;
    IDirect3DSurface frame{ nullptr };
    auto success = true;
    std::wstring failureFileName;
    try
    {
        {
            auto [currentFrame, color] = co_await TestCenterOfWindowAsync(device, window, cursorEnabled, captureType);
            frame = currentFrame;
            check_color(color, cursorColor);
        }
        
        cursorEnabled = false;
        {
            auto [currentFrame, color] = co_await TestCenterOfWindowAsync(device, window, cursorEnabled, captureType);
            frame = currentFrame;
            check_color(color, windowColor);
        }
    }
    catch (hresult_error const& error)
    {
        auto typeString = RemoteCaptureTypeToString(captureType);
        std::wstring cursorStateString(cursorEnabled ? L"Enabled" : L"Disabled");
        wprintf(L"Cursor disabled test (%s-%s) failed! 0x%08x - %s \n", typeString.c_str(), cursorStateString.c_str(), error.code().value, error.message().c_str());
        std::wstringstream stringStream;
        stringStream << L"cursor-disable_" << typeString.c_str() << L"_" << cursorStateString.c_str() << L"_failure.png";
        failureFileName = stringStream.str();
        success = false;
    }

    if (!success && frame != nullptr)
    {
        auto file = co_await SaveFrameAsync(device, frame, failureFileName.c_str());
        wprintf(L"Failure file saved: %s\n", file.Path().c_str());
    }

    co_return success;
}

IAsyncOperation<bool> CursorDisableTest(
    CompositorController const& compositorController, 
    IDirect3DDevice const& device, 
    DispatcherQueue const& compositorThreadQueue,
    bool monitor,
    bool window)
{
    if (winrt::Windows::Foundation::Metadata::ApiInformation::IsPropertyPresent(winrt::name_of<GraphicsCaptureSession>(), L"IsCursorCaptureEnabled"))
    {
        auto compositor = compositorController.Compositor();
        auto d3dDevice = GetDXGIInterfaceFromObject<ID3D11Device>(device);
        com_ptr<ID3D11DeviceContext> d3dContext;
        d3dDevice->GetImmediateContext(d3dContext.put());

        // Create the window on the compositor thread to borrow the message pump
        auto testWindow = co_await CreateSharedOnThreadAsync<DummyWindow>(compositorThreadQueue, L"CursorDisableTest");

        // The window animation may still be going on when we do a capture, which screws
        // up our assumptions on what the different pixels should be. Wait a bit to 
        // mitigate this.
        co_await std::chrono::milliseconds(250);

        // Setup a visual tree to make the window red
        auto target = testWindow->CreateWindowTarget(compositor);
        auto root = compositor.CreateSpriteVisual();
        root.RelativeSizeAdjustment({ 1, 1 });
        root.Brush(compositor.CreateColorBrush(Colors::Red()));
        target.Root(root);
        compositorController.Commit();

        // Create a square cursor that inverts content
        std::array<BYTE, 128> andMask;
        std::fill_n(andMask.data(), andMask.size(), 0xFF);
        std::array<BYTE, 128> orMask = andMask;
        wil::shared_hcursor newCursor(CreateCursor(GetModuleHandleW(nullptr), 16, 16, 32, 32, andMask.data(), orMask.data()));

        {
            CursorScope normalCursor(newCursor, CursorType::Normal);
            CursorScope waitCursor(newCursor, CursorType::Wait);
            CursorScope appStartingCursor(newCursor, CursorType::AppStarting);

            try
            {
                auto windowColor = Colors::Red();
                // Aqua and Cyan happen to be the inverse of Red
                auto cursorColor = Colors::Aqua();

                if (monitor)
                {
                    co_await TestCenterOfWindowAsync(RemoteCaptureType::Monitor, device, testWindow->m_window, windowColor, cursorColor);
                }
                if (window)
                {
                    co_await TestCenterOfWindowAsync(RemoteCaptureType::Window, device, testWindow->m_window, windowColor, cursorColor);
                }
            }
            catch (hresult_error const& error)
            {
                wprintf(L"Cursor disabled test failed! 0x%08x - %s \n", error.code().value, error.message().c_str());
                co_return false;
            }
        }
          
        CloseWindow(testWindow->m_window);
        co_return true;
    }
    else
    {
        wprintf(L"Metadata for IsCursorCaptureEnabled is not present on this system!");
        co_return false;
    }
}

IAsyncOperation<bool> HDRContentTest(CompositorController const& compositorController, IDirect3DDevice const& device, DispatcherQueue const& compositorThreadQueue, com_ptr<ID2D1Device> const& d2dDevice)
{
    auto compositor = compositorController.Compositor();
    auto d3dDevice = GetDXGIInterfaceFromObject<ID3D11Device>(device);
    com_ptr<ID3D11DeviceContext> d3dContext;
    d3dDevice->GetImmediateContext(d3dContext.put());

    try
    {
        // Create the window on the compositor thread to borrow the message pump
        auto window = co_await CreateSharedOnThreadAsync<DummyWindow>(compositorThreadQueue, L"HDR Content");

		auto compositionGraphics = util::CreateCompositionGraphicsDevice(compositor, d2dDevice.get());
		auto surface = compositionGraphics.CreateDrawingSurface(
			{ 800, 600 }, DirectXPixelFormat::R16G16B16A16Float, DirectXAlphaMode::Premultiplied);

		{
			util::SurfaceContext surfaceContext(surface);
			auto d2dContext = surfaceContext.GetDeviceContext();

			auto color = D2D1::ColorF(D2D1::ColorF::Yellow, 1.0f);
			auto boostValue = 3.0f;
			color.r *= boostValue;
			color.g *= boostValue;
			color.b *= boostValue;
			d2dContext->Clear(color);
		}

		auto brush = compositor.CreateSurfaceBrush(surface);
		brush.Stretch(CompositionStretch::Fill);

		auto visual = compositor.CreateSpriteVisual();
		visual.RelativeSizeAdjustment({ 1, 1 });
		visual.Brush(brush);

		auto target = window->CreateWindowTarget(compositor);
		target.Root(visual);

        compositorController.Commit();
        co_await winrt::resume_on_signal(window->Closed().get());
    }
    catch (hresult_error const& error)
    {
        wprintf(L"HDR Content test failed! 0x%08x - %s \n", error.code().value, error.message().c_str());
        co_return false;
    }

    co_return true;
}

IAsyncOperation<bool> DisplayAffinityTest(CompositorController const& compositorController, IDirect3DDevice const& device, DispatcherQueue const& compositorThreadQueue, testparams::DisplayAffinityMode const& mode)
{
    auto compositor = compositorController.Compositor();
    auto d3dDevice = GetDXGIInterfaceFromObject<ID3D11Device>(device);
    com_ptr<ID3D11DeviceContext> d3dContext;
    d3dDevice->GetImmediateContext(d3dContext.put());


    try
    {
        // Create the window on the compositor thread to borrow the message pump
        auto window = co_await CreateSharedOnThreadAsync<DummyWindow>(compositorThreadQueue, L"Display Affinity Test");
        auto visual = compositor.CreateSpriteVisual();
        visual.RelativeSizeAdjustment({ 1, 1 });
        visual.Brush(compositor.CreateColorBrush(Colors::Red()));

        auto target = window->CreateWindowTarget(compositor);
        target.Root(visual);

        auto wdaValue = WDA_NONE;
        switch (mode)
        {
        case testparams::DisplayAffinityMode::Monitor:
            wdaValue = WDA_MONITOR;
            break;
        case testparams::DisplayAffinityMode::ExcludeFromCapture:
            wdaValue = WDA_EXCLUDEFROMCAPTURE;
            break;
        default:
            break;
        }
        winrt::check_bool(SetWindowDisplayAffinity(window->m_window, wdaValue));

        compositorController.Commit();
        co_await winrt::resume_on_signal(window->Closed().get());
    }
    catch (hresult_error const& error)
    {
        std::wstring message = L"WDA_NONE";
        switch (mode)
        {
        case testparams::DisplayAffinityMode::Monitor:
            message = L"WDA_MONITOR";
            break;
        case testparams::DisplayAffinityMode::ExcludeFromCapture:
            message = L"WDA_EXCLUDEFROMCAPTURE";
            break;
        default:
            break;
        }
        wprintf(L"DisplayAffinityTest (%s) test failed! 0x%08x - %s \n", message.c_str(), error.code().value, error.message().c_str());
        co_return false;
    }

    co_return true;
}

std::wstring GetBuildString()
{
    wil::unique_hkey registryKey;
    winrt::check_hresult(RegOpenKeyExW(HKEY_LOCAL_MACHINE, L"SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion", 0, KEY_READ, &registryKey));

    DWORD dataSize = 0;
    winrt::check_hresult(RegGetValueW(registryKey.get(), nullptr, L"BuildLabEx", RRF_RT_REG_SZ, nullptr, nullptr, &dataSize));

    std::wstring buildString(dataSize / sizeof(wchar_t), 0);
    winrt::check_hresult(RegGetValueW(registryKey.get(), nullptr, L"BuildLabEx", RRF_RT_REG_SZ, nullptr, reinterpret_cast<void*>(buildString.data()), &dataSize));

    return buildString;
}

IAsyncAction MainAsync(testparams::TestParams params)
{
    // The compositor needs a DispatcherQueue. Since we aren't going to pump messages,
    // we can't use our current thread. Create a new one that is controlled by the dispatcher.
    auto dispatcherController = DispatcherQueueController::CreateOnDedicatedThread();
    auto compositorThread = dispatcherController.DispatcherQueue();
    // The tests aren't going to run on the compositor thread, so we need to control calling Commit. 
    auto compositorController = co_await CreateOnThreadAsync<CompositorController>(compositorThread);
    auto compositor = compositorController.Compositor();

    // Initialize D3D
    auto d3dDevice = util::CreateD3DDevice();
    com_ptr<ID3D11DeviceContext> d3dContext;
    d3dDevice->GetImmediateContext(d3dContext.put());
    auto dxgiDevice = d3dDevice.as<IDXGIDevice>();
    auto device = CreateDirect3DDevice(dxgiDevice.get());

    auto d2dFactory = util::CreateD2DFactory();
    auto d2dDevice = util::CreateD2DDevice(d2dFactory, d3dDevice);
    winrt::com_ptr<ID2D1DeviceContext> d2dContext;
    check_hresult(d2dDevice->CreateDeviceContext(D2D1_DEVICE_CONTEXT_OPTIONS_NONE, d2dContext.put()));

    // Tests
    auto success = std::visit(overloaded
    {
        [=](testparams::Alpha const&) -> bool { return TransparencyTest(compositorController, device).get(); },
        [=](testparams::FullscreenRate const& args) -> bool { return RenderRateTest(compositorController, device, compositorThread, args.FullscreenMode).get(); },
        [=](testparams::FullscreenTransition const& args) -> bool { return FullscreenTransitionTest(compositorController, device, compositorThread, args.TransitionMode).get(); },
        [=](testparams::HDRContent const&) -> bool { return HDRContentTest(compositorController, device, compositorThread, d2dDevice).get(); },
        [=](testparams::WindowRate const& args) -> bool { return WindowRenderRateTest(compositorController, device, args.WindowTitle, args.Delay, args.Duration).get(); },
        [=](testparams::CursorDisable const& args) -> bool { return CursorDisableTest(compositorController, device, compositorThread, args.Monitor, args.Window).get(); },
        [=](testparams::PCInfo const&) -> bool { auto buildString = GetBuildString(); wprintf(L"PC info: %s\n", buildString.c_str()); return true;  },
        [=](testparams::DisplayAffinity const& args) -> bool { return DisplayAffinityTest(compositorController, device, compositorThread, args.Mode).get();  },
        [=](std::monostate const&) -> bool { throw std::runtime_error("Invalid test params!"); },
    }, params);
}

int wmain(int argc, wchar_t* argv[])
{
    // NOTE: We don't properly scale any of the UI or properly respond to DPI changes, but none of 
    //       the UI is meant to be interacted with. This is just so that the tests do the right thing
    //       on high DPI machines.
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    init_apartment();

    FullscreenMaxRateWindow::RegisterWindowClass();
    DummyWindow::RegisterWindowClass();
    FullscreenTransitionWindow::RegisterWindowClass();

    auto app = wcliparse::Application<testparams::TestParams>(L"CaptureAdHocTest")
        .Version(L"0.2.0")
        .Author(L"Robert Mikhayelyan (rob.mikh@outlook.com)")
        .About(L"A small utility to test various parts of the Windows.Graphics.Capture API.")
        .Command(wcliparse::Command(L"alpha", testparams::TestParams(testparams::Alpha())))
        .Command(wcliparse::Command(L"fullscreen-rate", std::function(AdHocTestCliValidator::ValidateFullscreenRate))
            .Argument(wcliparse::Argument(L"--setfullscreenstate")
                .Alias(L"-sfs"))
            .Argument(wcliparse::Argument(L"--fullscreenwindow")
                .Alias(L"-fw")))
        .Command(wcliparse::Command(L"fullscreen-transition", std::function(AdHocTestCliValidator::ValidateFullscreenTransition))
            .Argument(wcliparse::Argument(L"--adhoc")
                .Alias(L"-ah"))
            .Argument(wcliparse::Argument(L"--automated")
                .Alias(L"-auto")))
        .Command(wcliparse::Command(L"window-rate", std::function(AdHocTestCliValidator::ValidateWindowRate))
            .Argument(wcliparse::Argument(L"--window")
                .Required(true)
                .Description(L"window title string")
                .TakesValue(true))
            .Argument(wcliparse::Argument(L"--delay")
                .Description(L"delay in seconds")
                .TakesValue(true))
            .Argument(wcliparse::Argument(L"--duration")
                .Description(L"duration in seconds")
                .TakesValue(true)
                .DefaultValue(L"10")))
        .Command(wcliparse::Command(L"cursor-disable", std::function(AdHocTestCliValidator::ValidateCursorDisable))
            .Argument(wcliparse::Argument(L"--monitor")
                .Alias(L"-m"))
            .Argument(wcliparse::Argument(L"--window")
                .Alias(L"-w")))
        .Command(wcliparse::Command(L"hdr-content", testparams::TestParams(testparams::HDRContent())))
        .Command(wcliparse::Command(L"display-affinity", std::function(AdHocTestCliValidator::ValidateDisplayAffinity))
            .Argument(wcliparse::Argument(L"--none")
                .Alias(L"-n"))
            .Argument(wcliparse::Argument(L"--monitor")
                .Alias(L"-m"))
            .Argument(wcliparse::Argument(L"--exclude")
                .Alias(L"-e")))
        .Command(wcliparse::Command(L"pc-info", testparams::TestParams(testparams::PCInfo())));

    testparams::TestParams params;
    try
    {
        params = app.Parse(argc, argv);
    }
    catch (std::runtime_error const&)
    {
        app.PrintUsage();
        return 1;
    }

    MainAsync(params).get();
    return 0;
}
