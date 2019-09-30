#include "pch.h"
#include "FullscreenMaxRateWindow.h"

void FullscreenMaxRateWindow::RegisterWindowClass()
{
    auto instance = winrt::check_pointer(GetModuleHandleW(nullptr));
    WNDCLASSEX wcex = { sizeof(wcex) };
    wcex.style = CS_HREDRAW | CS_VREDRAW;
    wcex.lpfnWndProc = WndProc;
    wcex.hInstance = instance;
    wcex.hIcon = LoadIconW(instance, IDI_APPLICATION);
    wcex.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    wcex.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
    wcex.lpszClassName = L"FullscreenMaxRateWindow";
    wcex.hIconSm = LoadIconW(wcex.hInstance, IDI_APPLICATION);
    winrt::check_bool(RegisterClassExW(&wcex));
}

FullscreenMaxRateWindow::FullscreenMaxRateWindow(FullscreenMode mode) 
{
    auto instance = winrt::check_pointer(GetModuleHandleW(nullptr));

    winrt::check_bool(CreateWindowW(L"FullscreenMaxRateWindow", L"FullscreenMaxRateWindow", WS_OVERLAPPEDWINDOW,
        CW_USEDEFAULT, CW_USEDEFAULT, 800, 600, nullptr, nullptr, instance, this));
    WINRT_ASSERT(m_window);

    ShowWindow(m_window, SW_SHOWDEFAULT);
    UpdateWindow(m_window);

    m_d3dDevice = CreateD3DDevice();
    m_d3dDevice->GetImmediateContext(m_d3dContext.put());
    m_swapChain = CreateDXGISwapChainForWindow(m_d3dDevice, 800, 600, DXGI_FORMAT_B8G8R8A8_UNORM, 2, m_window);

    // Get the adapter from our d3d device
    auto dxgiDevice = m_d3dDevice.as<IDXGIDevice>();
    winrt::com_ptr<IDXGIAdapter> adapter;
    winrt::check_hresult(dxgiDevice->GetAdapter(adapter.put()));

    // TODO: Enforce that we are running with only one output. (across all adapters)
    winrt::com_ptr<IDXGIOutput> output;
    winrt::check_hresult(adapter->EnumOutputs(0, output.put()));
    DXGI_OUTPUT_DESC outputDesc = {};
    winrt::check_hresult(output->GetDesc(&outputDesc));

    // TODO: Properly handle DPI
    auto width = outputDesc.DesktopCoordinates.right - outputDesc.DesktopCoordinates.left;
    auto height = outputDesc.DesktopCoordinates.bottom - outputDesc.DesktopCoordinates.top;

    switch (mode)
    {
    case FullscreenMode::SetFullscreenState:
        winrt::check_hresult(m_swapChain->SetFullscreenState(true, output.get()));
        break;
    case FullscreenMode::FullscreenWindow:
        winrt::check_bool(SetWindowLongPtrW(m_window, GWL_STYLE, WS_POPUP));
        winrt::check_bool(SetWindowPos(m_window, HWND_TOP, 0, 0, width, height, SWP_FRAMECHANGED | SWP_SHOWWINDOW));
        break;
    }
    ShowCursor(false);
    winrt::check_hresult(m_swapChain->ResizeBuffers(2, width, height, DXGI_FORMAT_B8G8R8A8_UNORM, 0));

    // Get the back buffer so we can clear it
    winrt::com_ptr<ID3D11Texture2D> backBuffer;
    winrt::check_hresult(m_swapChain->GetBuffer(0, winrt::guid_of<ID3D11Texture2D>(), backBuffer.put_void()));
    winrt::check_hresult(m_d3dDevice->CreateRenderTargetView(backBuffer.get(), nullptr, m_renderTargetView.put()));
}

FullscreenMaxRateWindow::~FullscreenMaxRateWindow()
{
    ShowCursor(true);
    // DXGI gets unhappy when we release a fullscreen swapchain :-/
    winrt::check_hresult(m_swapChain->SetFullscreenState(false, nullptr));
}

void FullscreenMaxRateWindow::Flip()
{
    float color[4] = { 1.0f, 0.0f, 0.0f, 1.0f }; // RGBA
    m_d3dContext->ClearRenderTargetView(m_renderTargetView.get(), color);
    
    DXGI_PRESENT_PARAMETERS presentParameters{};
    winrt::check_hresult(m_swapChain->Present1(0, 0, &presentParameters));
}

LRESULT FullscreenMaxRateWindow::MessageHandler(UINT const message, WPARAM const wparam, LPARAM const lparam)
{
    if (WM_DESTROY == message)
    {
        m_windowClosed = true;
        return 0;
    }

    switch (message)
    {
    case WM_KEYUP:
        if (wparam == VK_ESCAPE)
        {
            m_windowClosed = true;
            return 0;
        }
        break;
    }

    return base_type::MessageHandler(message, wparam, lparam);
}