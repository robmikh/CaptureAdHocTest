#include "pch.h"
#include "MarginsWindow.h"
#include "ControlsHelper.h"
#include "testutils.h"

namespace winrt
{
    using namespace Windows::Foundation;
    using namespace Windows::Graphics::Capture;
    using namespace Windows::Graphics::DirectX;
    using namespace Windows::Graphics::DirectX::Direct3D11;
    using namespace Windows::Graphics::Imaging;
    using namespace Windows::Storage;
    using namespace Windows::Storage::Streams;
    using namespace Windows::UI;
}

namespace util
{
    using namespace robmikh::common::desktop;
    using namespace robmikh::common::uwp;
}

const std::wstring MarginsWindow::ClassName = L"CaptureAdHocTest.MarginsWindow";

void MarginsWindow::RegisterWindowClass()
{
    auto instance = winrt::check_pointer(GetModuleHandleW(nullptr));
    WNDCLASSEX wcex = { sizeof(wcex) };
    wcex.style = CS_HREDRAW | CS_VREDRAW;
    wcex.lpfnWndProc = WndProc;
    wcex.hInstance = instance;
    wcex.hIcon = LoadIconW(instance, IDI_APPLICATION);
    wcex.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    wcex.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
    wcex.lpszClassName = ClassName.c_str();
    wcex.hIconSm = LoadIconW(wcex.hInstance, IDI_APPLICATION);
    winrt::check_bool(RegisterClassExW(&wcex));
}

MarginsWindow::MarginsWindow(std::wstring const& title, winrt::Color const& backgroundColor, uint32_t width, uint32_t height, bool showControls)
{
    auto instance = winrt::check_pointer(GetModuleHandleW(nullptr));
    m_windowClosed = wil::shared_event(wil::EventOptions::None);
    SetBackgroundColor(backgroundColor);

    m_initialWidth = width;
    m_initialHeight = height;

    WINRT_ASSERT(!m_window);
    WINRT_VERIFY(CreateWindowW(ClassName.c_str(), title.c_str(), WS_OVERLAPPEDWINDOW,
        CW_USEDEFAULT, CW_USEDEFAULT, width, height, nullptr, nullptr, instance, this));
    WINRT_ASSERT(m_window);

    ShowWindow(m_window, SW_SHOWDEFAULT);
    UpdateWindow(m_window);

    if (showControls)
    {
        CreateControls();
    }
}

MarginsWindow::~MarginsWindow()
{
}

void MarginsWindow::SetBackgroundColor(winrt::Color const& color)
{
    m_brush.reset(winrt::check_pointer(CreateSolidBrush(RGB(color.R, color.G, color.B))));
    winrt::check_bool(InvalidateRect(m_window, nullptr, false));
}

LRESULT MarginsWindow::MessageHandler(UINT const message, WPARAM const wparam, LPARAM const lparam)
{
    if (WM_DESTROY == message)
    {
        CloseWindow();
        return 0;
    }

    switch (message)
    {
    case WM_PAINT:
    {
        auto paint = PaintHelper(m_window);
        winrt::check_bool(FillRect(paint.HDC(), &paint.PaintStruct().rcPaint, m_brush.get()));
    }
    return 0;
    case WM_COMMAND:
    {
        auto command = HIWORD(wparam);
        auto hwnd = (HWND)lparam;
        switch (command)
        {
        case BN_CLICKED:
        {
            if (hwnd == m_toggleFullscreenButton)
            {
                Fullscreen(!m_fullscreen);
            }
            else if (hwnd == m_takeSnapshotButton)
            {
                TakeSnapshot();
            }
        }
        break;
        }
    }
    break;
    default:
        return base_type::MessageHandler(message, wparam, lparam);
    }

    return 0;
}

void MarginsWindow::CreateControls()
{
    auto controls = StackPanel(m_window, 10, 10, 200);

    m_toggleFullscreenButton = controls.CreateControl(ControlType::Button, L"Toggle fullscreen");
    m_takeSnapshotButton = controls.CreateControl(ControlType::Button, L"Take snapshot");
}

void MarginsWindow::Fullscreen(bool isFullscreen)
{
    if (m_fullscreen != isFullscreen)
    {
        m_fullscreen = isFullscreen;
        auto currentStyle = GetWindowLongW(m_window, GWL_STYLE);
        auto currentExStyle = GetWindowLongW(m_window, GWL_EXSTYLE);
        auto width = 0;
        auto height = 0;
        auto x = 0;
        auto y = 0;

        auto monitor = MonitorFromWindow(m_window, MONITOR_DEFAULTTOPRIMARY);
        auto monitorInfo = CreateWin32Struct<MONITORINFO>();
        winrt::check_bool(GetMonitorInfoW(monitor, &monitorInfo));
        auto workAreaWidth = monitorInfo.rcWork.right - monitorInfo.rcWork.left;
        auto workAreaHeight = monitorInfo.rcWork.bottom - monitorInfo.rcWork.top;

        HWND insertAfter = nullptr;

        if (m_fullscreen)
        {
            currentStyle &= ~(WS_CAPTION | WS_THICKFRAME | WS_MINIMIZEBOX | WS_MAXIMIZEBOX | WS_SYSMENU);
            currentExStyle &= ~(WS_EX_DLGMODALFRAME | WS_EX_CLIENTEDGE | WS_EX_STATICEDGE);

            x = monitorInfo.rcWork.left;
            y = monitorInfo.rcWork.top;
            width = workAreaWidth;
            height = workAreaHeight;

            insertAfter = HWND_TOPMOST;
        }
        else 
        {
            currentStyle = WS_OVERLAPPEDWINDOW;
            currentExStyle = 0;
            x = (workAreaWidth - m_initialWidth) / 2;
            y = (workAreaHeight - m_initialHeight) / 2;
            width = m_initialWidth;
            height = m_initialHeight;

            insertAfter = HWND_NOTOPMOST;
        }

        SetWindowLongW(m_window, GWL_STYLE, currentStyle | WS_VISIBLE);
        SetWindowLongW(m_window, GWL_EXSTYLE, currentExStyle);
        winrt::check_bool(SetWindowPos(m_window, insertAfter, x, y, width, height, SWP_FRAMECHANGED));
        ShowWindow(m_window, SW_SHOW);
    }
}

winrt::IAsyncAction SaveBitmapAsync(std::vector<byte> bits, uint32_t width, uint32_t height)
{
    // Get a file to save the screenshot
    auto currentPath = std::filesystem::current_path();
    auto folder = co_await winrt::StorageFolder::GetFolderFromPathAsync(currentPath.wstring());
    auto file = co_await folder.CreateFileAsync(L"marginsSnapshot.png", winrt::CreationCollisionOption::ReplaceExisting);

    auto stream = co_await file.OpenAsync(winrt::FileAccessMode::ReadWrite);
    auto encoder = co_await winrt::BitmapEncoder::CreateAsync(winrt::BitmapEncoder::PngEncoderId(), stream);
    encoder.SetPixelData(
        winrt::BitmapPixelFormat::Bgra8,
        winrt::BitmapAlphaMode::Premultiplied,
        width,
        height,
        1.0,
        1.0,
        bits);
    co_await encoder.FlushAsync();
}

winrt::fire_and_forget MarginsWindow::TakeSnapshot()
{
    // TODO: Don't create a new d3d device each time
    auto d3dDevice = util::CreateD3DDevice();
    winrt::com_ptr<ID3D11DeviceContext> d3dContext;
    d3dDevice->GetImmediateContext(d3dContext.put());
    auto dxgiDevice = d3dDevice.as<IDXGIDevice>();
    auto device = CreateDirect3DDevice(dxgiDevice.get());

    auto item = util::CreateCaptureItemForWindow(m_window);
    auto framePool = winrt::Direct3D11CaptureFramePool::Create(
        device,
        winrt::DirectXPixelFormat::B8G8R8A8UIntNormalized,
        1,
        item.Size());
    auto session = framePool.CreateCaptureSession(item);

    winrt::com_ptr<ID3D11Texture2D> result;
    wil::shared_event captureEvent(wil::EventOptions::ManualReset);
    framePool.FrameArrived([session, d3dDevice, d3dContext, &result, captureEvent](auto& framePool, auto&)
        {
            auto frame = framePool.TryGetNextFrame();
            auto frameTexture = GetDXGIInterfaceFromObject<ID3D11Texture2D>(frame.Surface());

            // Make a copy of the texture
            D3D11_TEXTURE2D_DESC desc = {};
            frameTexture->GetDesc(&desc);
            // Clear flags that we don't need
            desc.Usage = D3D11_USAGE_STAGING;
            desc.BindFlags = 0;
            desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
            desc.MiscFlags = 0;
            winrt::com_ptr<ID3D11Texture2D> textureCopy;
            winrt::check_hresult(d3dDevice->CreateTexture2D(&desc, nullptr, textureCopy.put()));
            d3dContext->CopyResource(textureCopy.get(), frameTexture.get());

            result = textureCopy;

            // End the capture
            session.Close();
            framePool.Close();

            // Signal that we're done
            captureEvent.SetEvent();
        });
    session.StartCapture();

    // Don't return until the capture is finished
    co_await winrt::resume_on_signal(captureEvent.get());
    WINRT_ASSERT(result != nullptr);

    D3D11_TEXTURE2D_DESC desc = {};
    result->GetDesc(&desc);
    WINRT_ASSERT(desc.Format == DXGI_FORMAT_B8G8R8A8_UNORM);
    auto bytesPerPixel = 4;

    // Get the bits
    D3D11_MAPPED_SUBRESOURCE mapped = {};
    winrt::check_hresult(d3dContext->Map(result.get(), 0, D3D11_MAP_READ, 0, &mapped));

    std::vector<byte> bits(desc.Width * desc.Height * bytesPerPixel, 0);
    auto source = reinterpret_cast<byte*>(mapped.pData);
    auto dest = bits.data();
    for (auto i = 0; i < (int)desc.Height; i++)
    {
        memcpy(dest, source, desc.Width * bytesPerPixel);

        source += mapped.RowPitch;
        dest += desc.Width * bytesPerPixel;
    }

    d3dContext->Unmap(result.get(), 0);

    // Save
    co_await SaveBitmapAsync(bits, desc.Width, desc.Height);
}