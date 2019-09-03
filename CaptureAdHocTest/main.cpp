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

IAsyncOperation<StorageFile> SaveFrameAsync(IDirect3DDevice const& device, IDirect3DSurface const& surface, std::wstring const& fileName)
{
    // Get a file to save the screenshot
    auto currentPath = std::filesystem::current_path();
    auto folder = co_await StorageFolder::GetFolderFromPathAsync(currentPath.wstring());
    auto file = co_await folder.CreateFileAsync(fileName.c_str(), CreationCollisionOption::ReplaceExisting);

    // Get the file stream
    auto randomAccessStream = co_await file.OpenAsync(FileAccessMode::ReadWrite);
    auto stream = CreateStreamFromRandomAccessStream(randomAccessStream);

    // Get the DXGI surface from the frame
    auto dxgiFrameTexture = GetDXGIInterfaceFromObject<IDXGISurface>(surface);

    // Create graphics resources
    auto d3dDevice = GetDXGIInterfaceFromObject<ID3D11Device>(device);

    // Get a D2D bitmap for our snapshot
    // TODO: Since this sample doesn't use D2D any other way, it may be better to map 
    //       the pixels manually and hand them to WIC. However, using d2d is easier for now.
    auto d2dFactory = CreateD2DFactory();
    auto d2dDevice = CreateD2DDevice(d2dFactory, d3dDevice);
    winrt::com_ptr<ID2D1DeviceContext> d2dContext;
    check_hresult(d2dDevice->CreateDeviceContext(D2D1_DEVICE_CONTEXT_OPTIONS_NONE, d2dContext.put()));
    com_ptr<ID2D1Bitmap1> d2dBitmap;
    check_hresult(d2dContext->CreateBitmapFromDxgiSurface(dxgiFrameTexture.get(), nullptr, d2dBitmap.put()));

    // Encode the snapshot
    auto wicFactory = CreateWICFactory();
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

enum class Commands
{
    Alpha,
    FullscreenRate,
    WindowRate,
    PCInfo,
    Help
};

struct CommandOptions
{
    Commands selected = Commands::Help;
    FullscreenMode fullscreenMode = FullscreenMode::SetFullscreenState;
    std::wstring windowTitle;
    int delayInSeconds = 0;
    int durationInSeconds = 10;
};

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
        wprintf(L"Transparency test failed! 0x%08x - %s \n", error.code(), error.message().c_str());
        success = false;
    }

    if (!success && frame != nullptr)
    {
        auto file = co_await SaveFrameAsync(device, frame, L"alpha_failure.png");
        wprintf(L"Failure file saved: %s\n", file.Path().c_str());
    }

    co_return success;
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

IAsyncAction MainAsync(CommandOptions options)
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
    switch (options.selected)
    {
    case Commands::Alpha:
    {
        auto transparencyPassed = co_await TransparencyTest(compositorController, device);
    }
    break;
    case Commands::FullscreenRate:
    {
        auto renderRatePassed = co_await RenderRateTest(compositorController, device, compositorThread, options.fullscreenMode);
    }
    break;
    case Commands::WindowRate:
    {
        auto delay = std::chrono::seconds(options.delayInSeconds);
        auto duration = std::chrono::seconds(options.durationInSeconds);

        auto renderRatePassed = co_await WindowRenderRateTest(compositorController, device, options.windowTitle, delay, duration);
    }
    break;
    case Commands::PCInfo:
    {
        auto buildString = GetBuildString();
        wprintf(L"PC info: %s\n", buildString.c_str());
    }
    break;
    }
}

bool TryParseCommandOptions(wcliparse::Matches<Commands>& matches, CommandOptions& options)
{
    options.selected = matches.Command();
    switch (options.selected)
    {
    case Commands::FullscreenRate:
    {
        auto setFullscreenState = matches.IsPresent(L"--setfullscreenstate");
        auto fullscreenWindow = matches.IsPresent(L"--fullscreenwindow");
        if (setFullscreenState == fullscreenWindow)
        {
            return false;
        }

        options.fullscreenMode = setFullscreenState ? FullscreenMode::SetFullscreenState : FullscreenMode::FullscreenWindow;
    }
        break;
    case Commands::WindowRate:
    {
        options.windowTitle = matches.ValueOf(L"--window");

        if (matches.IsPresent(L"--delay"))
        {
            auto delayString = matches.ValueOf(L"--delay");
            options.delayInSeconds = std::stoi(delayString);
        }

        if (matches.IsPresent(L"--duration"))
        {
            auto durationString = matches.ValueOf(L"--duration");
            options.durationInSeconds = std::stoi(durationString);
        }
    }
        break;
    }

    return true;
}

int wmain(int argc, wchar_t* argv[])
{
    init_apartment();

    FullscreenMaxRateWindow::RegisterWindowClass();

    auto app = wcliparse::Application<Commands>(L"CaptureAdHocTest")
        .Version(L"0.1.0")
        .Author(L"Robert Mikhayelyan (rob.mikh@outlook.com)")
        .About(L"A small utility to test various parts of the Windows.Graphics.Capture API.")
        .Command(wcliparse::Command(L"alpha", Commands::Alpha))
        .Command(wcliparse::Command(L"fullscreen-rate", Commands::FullscreenRate)
            .Argument(wcliparse::Argument(L"--setfullscreenstate")
                .Alias(L"-sfs"))
            .Argument(wcliparse::Argument(L"--fullscreenwindow")
                .Alias(L"-fw")))
        .Command(wcliparse::Command(L"window-rate", Commands::WindowRate)
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
        .Command(wcliparse::Command(L"pc-info", Commands::PCInfo));

    CommandOptions options;
    try
    {
        auto matches = app.Parse(argc, argv);
        if (!TryParseCommandOptions(matches, options))
        {
            throw std::runtime_error("Invalid input!");
        }
    }
    catch (std::runtime_error const& error)
    {
        app.PrintUsage();
        return 1;
    }

    MainAsync(options).get();
    return 0;
}
