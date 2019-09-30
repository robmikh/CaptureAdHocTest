#include "pch.h"
#include "HDRContentWindow.h"

namespace winrt
{
    using namespace Windows::Foundation;
    using namespace Windows::Graphics::DirectX;
    using namespace Windows::Graphics::DirectX::Direct3D11;
    using namespace Windows::UI::Composition;
    using namespace Windows::UI::Composition::Desktop;
}

const std::wstring HDRContentWindow::ClassName = L"HDRContentWindow";

void HDRContentWindow::RegisterWindowClass()
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

HDRContentWindow::HDRContentWindow(winrt::Compositor const& compositor, winrt::com_ptr<ID2D1Device> const& d2dDevice)
{
    auto instance = winrt::check_pointer(GetModuleHandleW(nullptr));
    m_windowClosed = wil::shared_event(wil::EventOptions::None);

    winrt::check_bool(CreateWindowW(ClassName.c_str(), L"HDRContentWindow", WS_OVERLAPPEDWINDOW,
        CW_USEDEFAULT, CW_USEDEFAULT, 800, 600, nullptr, nullptr, instance, this));
    WINRT_ASSERT(m_window);

    ShowWindow(m_window, SW_SHOWDEFAULT);
    UpdateWindow(m_window);

    m_compositor = compositor;
    m_d2dDevice = d2dDevice;

    m_compositionGraphics = CreateCompositionGraphicsDevice(m_compositor, m_d2dDevice.get());
    auto surface = m_compositionGraphics.CreateDrawingSurface(
        { 800, 600 }, winrt::DirectXPixelFormat::R16G16B16A16Float, winrt::DirectXAlphaMode::Premultiplied);

    {
        SurfaceContext surfaceContext(surface);
        auto d2dContext = surfaceContext.GetDeviceContext();

        d2dContext->Clear(D2D1::ColorF(D2D1::ColorF::Yellow, 1.0f));
    }
    
    auto visual = m_compositor.CreateSpriteVisual();
    visual.RelativeSizeAdjustment({ 1, 1 });
    visual.Brush(m_compositor.CreateSurfaceBrush(surface));

    m_target = CreateWindowTarget(m_compositor);
    m_target.Root(visual);
}

HDRContentWindow::~HDRContentWindow()
{

}

LRESULT HDRContentWindow::MessageHandler(UINT const message, WPARAM const wparam, LPARAM const lparam)
{
    if (WM_DESTROY == message)
    {
        CloseWindow();
        return 0;
    }

    switch (message)
    {
    case WM_KEYUP:
        if (wparam == VK_ESCAPE)
        {
            CloseWindow();
            return 0;
        }
        break;
    }

    return base_type::MessageHandler(message, wparam, lparam);
}