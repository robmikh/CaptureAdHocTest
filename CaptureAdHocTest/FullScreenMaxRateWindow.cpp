#include "pch.h"
#include "FullScreenMaxRateWindow.h"

void FullScreenMaxRateWindow::RegisterWindowClass()
{
    auto instance = GetModuleHandleW(nullptr);
    winrt::check_bool(instance);

    WNDCLASSEX wcex = { sizeof(wcex) };
    wcex.style = CS_HREDRAW | CS_VREDRAW;
    wcex.lpfnWndProc = WndProc;
    wcex.hInstance = instance;
    wcex.hIcon = LoadIconW(instance, IDI_APPLICATION);
    wcex.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    wcex.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
    wcex.lpszClassName = L"FullScreenMaxRateWindow";
    wcex.hIconSm = LoadIconW(wcex.hInstance, IDI_APPLICATION);
    winrt::check_bool(RegisterClassExW(&wcex));
}

FullScreenMaxRateWindow::FullScreenMaxRateWindow() 
{
    auto instance = GetModuleHandleW(nullptr);
    winrt::check_bool(instance);

    winrt::check_bool(CreateWindowW(L"FullScreenMaxRateWindow", L"FullScreenMaxRateWindow", WS_OVERLAPPEDWINDOW,
        CW_USEDEFAULT, CW_USEDEFAULT, 800, 600, nullptr, nullptr, instance, this));
    WINRT_ASSERT(m_window);

    ShowWindow(m_window, SW_SHOWDEFAULT);
    UpdateWindow(m_window);

    m_d3dDevice = CreateD3DDevice();
    m_d3dDevice->GetImmediateContext(m_d3dContext.put());
    m_swapChain = CreateDXGISwapChainForWindow(m_d3dDevice, 800, 600, DXGI_FORMAT_B8G8R8A8_UNORM, 3, m_window);

    winrt::com_ptr<ID3D11Texture2D> backBuffer;
    winrt::check_hresult(m_swapChain->GetBuffer(0, winrt::guid_of<ID3D11Texture2D>(), backBuffer.put_void()));
    winrt::check_hresult(m_d3dDevice->CreateRenderTargetView(backBuffer.get(), nullptr, m_renderTargetView.put()));

    winrt::check_hresult(m_swapChain->SetFullscreenState(true, nullptr));
    winrt::check_hresult(m_swapChain->ResizeBuffers(2, 800, 600,
        DXGI_FORMAT_B8G8R8A8_UNORM, 0));
}

void FullScreenMaxRateWindow::Flip()
{
    float color[4] = { 1.0f, 0.0f, 0.0f, 1.0f }; // RGBA
    m_d3dContext->ClearRenderTargetView(m_renderTargetView.get(), color);
    
    DXGI_PRESENT_PARAMETERS presentParameters{};
    winrt::check_hresult(m_swapChain->Present1(0, 0, &presentParameters));
}

LRESULT FullScreenMaxRateWindow::MessageHandler(UINT const message, WPARAM const wparam, LPARAM const lparam)
{
    if (WM_DESTROY == message)
    {
        return 0;
    }

    return base_type::MessageHandler(message, wparam, lparam);
}