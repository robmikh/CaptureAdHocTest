#include "pch.h"
#include "CaptureSnapshot.h"
#include "FullscreenMaxRateWindow.h"
#include "DummyWindow.h"
#include "cliParser.h"

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

template<typename T>
inline void check_color(T value, winrt::Windows::UI::Color const& expected)
{
    if (value != expected)
    {
        std::wstringstream stringStream;
        stringStream << L"Color comparison failed!";
        stringStream << std::endl;
        stringStream << L"\tValue: ( B: " << value.B << L", G: " << value.G << ", R: " << value.R << ", A: " << value.A << " )";
        stringStream << std::endl;
        stringStream << L"\tExpected: ( B: " << expected.B << L", G: " << expected.G << ", R: " << expected.R << ", A: " << expected.A << " )";
        stringStream << std::endl;
        throw hresult_error(E_FAIL, stringStream.str());
    }
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

class MappedTexture
{
public:
    struct BGRAPixel
    {
        BYTE B;
        BYTE G;
        BYTE R;
        BYTE A;

        bool operator==(const Color& color) { return B == color.B && G == color.G && R == color.R && A == color.A; }
        bool operator!=(const Color& color) { return !(*this == color); }

        winrt::Windows::UI::Color to_color() { return winrt::Windows::UI::Color{ A, R, G, B }; }
    };

    MappedTexture(com_ptr<ID3D11DeviceContext> d3dContext, com_ptr<ID3D11Texture2D> texture)
    {
        m_d3dContext = d3dContext;
        m_texture = texture;
        m_texture->GetDesc(&m_textureDesc);
        check_hresult(m_d3dContext->Map(m_texture.get(), 0, D3D11_MAP_READ, 0, &m_mappedData));
    }
    ~MappedTexture()
    {
        m_d3dContext->Unmap(m_texture.get(), 0);
    }

    BGRAPixel ReadBGRAPixel(uint32_t x, uint32_t y)
    {
        if (x < m_textureDesc.Width && y < m_textureDesc.Height)
        {
            auto bytesPerPixel = 4;
            auto data = static_cast<BYTE*>(m_mappedData.pData);
            auto offset = (m_mappedData.RowPitch * y) + (x * bytesPerPixel);
            auto B = data[offset + 0];
            auto G = data[offset + 1];
            auto R = data[offset + 2];
            auto A = data[offset + 3];
            return BGRAPixel{ B, G, R, A };
        }
        else
        {
            throw hresult_out_of_bounds();
        }
    }

private:
    com_ptr<ID3D11DeviceContext> m_d3dContext;
    com_ptr<ID3D11Texture2D> m_texture;
    D3D11_MAPPED_SUBRESOURCE m_mappedData = {};
    D3D11_TEXTURE2D_DESC m_textureDesc = {};
};

IAsyncOperation<bool> TransparencyTest(CompositorController const& compositorController, IDirect3DDevice const& device)
{
    auto compositor = compositorController.Compositor();
    auto d3dDevice = GetDXGIInterfaceFromObject<ID3D11Device>(device);
    com_ptr<ID3D11DeviceContext> d3dContext;
    d3dDevice->GetImmediateContext(d3dContext.put());

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
        auto frame = co_await asyncOperation;
        auto frameTexture = GetDXGIInterfaceFromObject<ID3D11Texture2D>(frame);

        // Map the texture and check the image
        {
            auto mapped = MappedTexture(d3dContext, frameTexture);

            check_color(mapped.ReadBGRAPixel(50, 50), Colors::Red());
            check_color(mapped.ReadBGRAPixel(5, 5), Colors::Transparent());
        }
    }
    catch (hresult_error const& error)
    {
        wprintf(L"Transparency test failed! 0x%08x - %s \n", error.code(), error.message().c_str());
        co_return false;
    }

    co_return true;
}

IAsyncOperation<bool> RenderRateTest(CompositorController const& compositorController, IDirect3DDevice const& device, DispatcherQueue const& compositorThreadQueue, FullscreenMode mode)
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
        auto item = CreateCaptureItemForWindow(window->m_window);
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
        wprintf(L"Render rate test failed! 0x%08x - %s \n", error.code(), error.message().c_str());
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
        auto item = CreateCaptureItemForWindow(window);
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
        wprintf(L"Render rate test failed! 0x%08x - %s \n", error.code(), error.message().c_str());
        co_return false;
    }

    co_return true;
}

// https://docs.microsoft.com/en-us/windows/win32/api/winuser/nf-winuser-setsystemcursor
enum class CursorType : DWORD
{
    Normal = 32512,
    Wait = 32514,
    AppStarting = 32650
};

struct CursorScope
{
    CursorScope(wil::shared_hcursor const& cursor, CursorType cursorType)
    {
        m_cursor.reset(CopyCursor(cursor.get()));
        m_type = cursorType;

        m_oldCursor.reset(CopyCursor(LoadCursorW(nullptr, MAKEINTRESOURCEW((DWORD)m_type))));

        winrt::check_bool(SetSystemCursor(m_cursor.get(), (DWORD)m_type));
    }

    ~CursorScope()
    {
        winrt::check_bool(SetSystemCursor(m_oldCursor.get(), (DWORD)m_type));
    }

private:
    wil::unique_hcursor m_cursor;
    wil::unique_hcursor m_oldCursor;
    CursorType m_type;
};

template <typename T>
T CreateWin32Struct()
{
    T thing = {};
    thing.cbSize = sizeof(T);
    return thing;
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

IAsyncOperation<Color> TestCenterOfWindowAsync(IDirect3DDevice const& device, HWND window, bool cursorEnabled, RemoteCaptureType captureType)
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

        item = CreateCaptureItemForMonitor(monitor);
    }
    break;
    case RemoteCaptureType::Window:
    {
        // Find where the window is
        RECT rect = {};
        winrt::check_bool(GetWindowRect(window, &rect));
        mouseX -= rect.left;
        mouseY -= rect.top;

        item = CreateCaptureItemForWindow(window);
    }
    break;
    }

    auto frame = co_await CaptureSnapshot::TakeAsync(device, item, true, cursorEnabled);
    auto frameTexture = GetDXGIInterfaceFromObject<ID3D11Texture2D>(frame);

    // Map the texture and check the image
    auto mapped = MappedTexture(d3dContext, frameTexture);
    co_return mapped.ReadBGRAPixel(mouseX, mouseY).to_color();
}

IAsyncOperation<bool> TestCenterOfWindowAsync(RemoteCaptureType captureType, IDirect3DDevice const& device, HWND window, Color windowColor, Color cursorColor)
{
    auto cursorEnabled = true;
    try
    {
        auto color = co_await TestCenterOfWindowAsync(device, window, cursorEnabled, captureType);
        check_color(color, cursorColor);

        cursorEnabled = false;
        color = co_await TestCenterOfWindowAsync(device, window, cursorEnabled, captureType);
        check_color(color, windowColor);

    }
    catch (hresult_error const& error)
    {
        auto typeString = RemoteCaptureTypeToString(captureType);
        wprintf(L"Cursor disabled test (%s-%s) failed! 0x%08x - %s \n", typeString.c_str(), cursorEnabled ? L"Enabled" : L"Disabled", error.code(), error.message().c_str());
        co_return false;
    }

    co_return true;
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
        auto window = co_await CreateSharedOnThreadAsync<DummyWindow>(compositorThreadQueue);

        // Setup a visual tree to make the window red
        auto target = CreateDesktopWindowTarget(compositor, window->m_window, false);
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
                    co_await TestCenterOfWindowAsync(RemoteCaptureType::Monitor, device, window->m_window, windowColor, cursorColor);
                }
                if (window)
                {
                    co_await TestCenterOfWindowAsync(RemoteCaptureType::Window, device, window->m_window, windowColor, cursorColor);
                }
            }
            catch (hresult_error const& error)
            {
                wprintf(L"Cursor disabled test failed! 0x%08x - %s \n", error.code(), error.message().c_str());
                co_return false;
            }
        }
          
        CloseWindow(window->m_window);
        co_return true;
    }
    else
    {
        wprintf(L"Metadata for IsCursorCaptureEnabled is not present on this system!");
        co_return false;
    }
}

IAsyncAction MainAsync(std::vector<std::wstring> args)
{
    // The compositor needs a DispatcherQueue. Since we aren't going to pump messages,
    // we can't use our current thread. Create a new one that is controlled by the dispatcher.
    auto dispatcherController = DispatcherQueueController::CreateOnDedicatedThread();
    auto compositorThread = dispatcherController.DispatcherQueue();
    // The tests aren't going to run on the compositor thread, so we need to control calling Commit. 
    auto compositorController = co_await CreateOnThreadAsync<CompositorController>(compositorThread);
    auto compositor = compositorController.Compositor();

    // Initialize D3D
    auto d3dDevice = CreateD3DDevice();
    com_ptr<ID3D11DeviceContext> d3dContext;
    d3dDevice->GetImmediateContext(d3dContext.put());
    auto dxgiDevice = d3dDevice.as<IDXGIDevice>();
    auto device = CreateDirect3DDevice(dxgiDevice.get());

    auto d2dFactory = CreateD2DFactory();
    auto d2dDevice = CreateD2DDevice(d2dFactory, d3dDevice);
    winrt::com_ptr<ID2D1DeviceContext> d2dContext;
    check_hresult(d2dDevice->CreateDeviceContext(D2D1_DEVICE_CONTEXT_OPTIONS_NONE, d2dContext.put()));

    // Tests
    if (args.size() <= 0)
    {
        PrintUsage();
        co_return;
    }
    auto command = args[0];
    args.erase(args.begin());

    if (command == L"alpha")
    {
        auto transparencyPassed = co_await TransparencyTest(compositorController, device);
    }
    else if (command == L"fullscreen-rate")
    {
        // TODO: change this to an enum in the cli
        auto setFullscreenState = GetFlag(args, L"--setfullscreenstate", L"-sfs");
        auto fullscreenWindow = GetFlag(args, L"--fullscreenwindow", L"-fw");
        if (setFullscreenState == fullscreenWindow)
        {
            PrintUsage();
            co_return;
        }

        auto mode = FullscreenMode::SetFullscreenState;
        if (fullscreenWindow)
        {
            mode = FullscreenMode::FullscreenWindow;
        }

        auto renderRatePassed = co_await RenderRateTest(compositorController, device, compositorThread, mode);
    }
    else if (command == L"window-rate")
    {
        auto windowName = GetFlagValue(args, L"--window");
        if (windowName.empty())
        {
            PrintUsage();
            co_return;
        }

        auto delay = GetFlagValueWithDefault(args, L"--delay", std::chrono::seconds(0));
        auto duration = GetFlagValueWithDefault(args, L"--duration", std::chrono::seconds(10));

        auto renderRatePassed = co_await WindowRenderRateTest(compositorController, device, windowName, delay, duration);
    }
    else if (command == L"cursor-disable")
    {
        auto monitor = GetFlag(args, L"--monitor", L"-m");
        auto window = GetFlag(args, L"--window", L"-w");
        if (!monitor && !window)
        {
            std::wcout << L"Nothing to test!" << std::endl;
            PrintUsage();
            co_return;
        }

        auto cursorPassed = co_await CursorDisableTest(compositorController, device, compositorThread, monitor, window);
    }
    else
    {
        std::wcout << L"Unknown command! \"" << command << L"\"" << std::endl;
        PrintUsage();
        co_return;
    }
}

int wmain(int argc, wchar_t* argv[])
{
    init_apartment();

    FullscreenMaxRateWindow::RegisterWindowClass();
    DummyWindow::RegisterWindowClass();

    std::vector<std::wstring> args(argv + 1, argv + argc);
    MainAsync(args).get();
}
