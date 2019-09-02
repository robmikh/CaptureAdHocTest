#include "pch.h"
#include "CaptureSnapshot.h"
#include "FullscreenMaxRateWindow.h"
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
    else if (command == L"pc-info")
    {
        auto buildString = GetBuildString();
        wprintf(L"PC info: %s\n", buildString.c_str());
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

    std::vector<std::wstring> args(argv + 1, argv + argc);
    MainAsync(args).get();
}
