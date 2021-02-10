#include "pch.h"
#include "FullscreenTransitionWindow.h"

namespace util
{
    using namespace robmikh::common::desktop;
    using namespace robmikh::common::uwp;
}

using namespace testparams;

const std::wstring FullscreenTransitionWindow::ClassName = L"CaptureAdHocTest.FullscreenTransitionWindow";

void FullscreenTransitionWindow::RegisterWindowClass()
{
    auto instance = winrt::check_pointer(GetModuleHandleW(nullptr));
    WNDCLASSEX wcex = { sizeof(wcex) };
    wcex.style = CS_HREDRAW | CS_VREDRAW | CS_DBLCLKS;
    wcex.lpfnWndProc = WndProc;
    wcex.hInstance = instance;
    wcex.hIcon = LoadIconW(instance, IDI_APPLICATION);
    wcex.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    wcex.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
    wcex.lpszClassName = ClassName.c_str();
    wcex.hIconSm = LoadIconW(wcex.hInstance, IDI_APPLICATION);
    winrt::check_bool(RegisterClassExW(&wcex));
}

FullscreenTransitionWindow::FullscreenTransitionWindow(FullscreenTransitionTestMode mode)
{
    auto instance = winrt::check_pointer(GetModuleHandleW(nullptr));
    m_windowClosed = wil::shared_event(wil::EventOptions::None);
    m_mode = mode;

    winrt::check_bool(CreateWindowW(ClassName.c_str(), L"FullscreenTransitionWindow", WS_OVERLAPPEDWINDOW,
        CW_USEDEFAULT, CW_USEDEFAULT, 800, 600, nullptr, nullptr, instance, this));
    WINRT_ASSERT(m_window);

    ShowWindow(m_window, SW_SHOWDEFAULT);
    UpdateWindow(m_window);

    m_d3dDevice = util::CreateD3DDevice();
    m_d3dDevice->GetImmediateContext(m_d3dContext.put());
    m_swapChain = util::CreateDXGISwapChainForWindow(m_d3dDevice, 800, 600, DXGI_FORMAT_B8G8R8A8_UNORM, 2, m_window);

    // Get the back buffer so we can clear it
    winrt::com_ptr<ID3D11Texture2D> backBuffer;
    winrt::check_hresult(m_swapChain->GetBuffer(0, winrt::guid_of<ID3D11Texture2D>(), backBuffer.put_void()));
    winrt::check_hresult(m_d3dDevice->CreateRenderTargetView(backBuffer.get(), nullptr, m_renderTargetView.put()));
}

FullscreenTransitionWindow::~FullscreenTransitionWindow()
{
    // We can't release the swap chain when it's fullscreen,
    // so set fullscreen to false just in case.
    m_swapChain->SetFullscreenState(false, nullptr);
}

void FullscreenTransitionWindow::Fullscreen(bool isFullscreen)
{
    if (isFullscreen == m_fullscreen)
    {
        return;
    }
    m_fullscreen = isFullscreen;
    m_renderTargetView = nullptr;

    if (m_fullscreen)
    {
        // Get the adapter from our d3d device
        auto dxgiDevice = m_d3dDevice.as<IDXGIDevice>();
        winrt::com_ptr<IDXGIAdapter> adapter;
        winrt::check_hresult(dxgiDevice->GetAdapter(adapter.put()));

        // TODO: Alow the selection of other adapters/outputs for convenience 
        winrt::com_ptr<IDXGIOutput> output;
        winrt::check_hresult(adapter->EnumOutputs(0, output.put()));
        DXGI_OUTPUT_DESC outputDesc = {};
        winrt::check_hresult(output->GetDesc(&outputDesc));

        // TODO: Properly handle DPI
        auto width = outputDesc.DesktopCoordinates.right - outputDesc.DesktopCoordinates.left;
        auto height = outputDesc.DesktopCoordinates.bottom - outputDesc.DesktopCoordinates.top;

        winrt::check_hresult(m_swapChain->SetFullscreenState(true, output.get()));
        winrt::check_hresult(m_swapChain->ResizeBuffers(2, width, height, DXGI_FORMAT_B8G8R8A8_UNORM, 0));
    }
    else
    {
        RECT rect = {};
        winrt::check_bool(GetWindowRect(m_window, &rect));
        auto width = rect.right - rect.left;
        auto height = rect.bottom - rect.top;

        winrt::check_hresult(m_swapChain->SetFullscreenState(false, nullptr));
        winrt::check_hresult(m_swapChain->ResizeBuffers(2, width, height, DXGI_FORMAT_B8G8R8A8_UNORM, 0));
    }

    // Get the back buffer so we can clear it
    winrt::com_ptr<ID3D11Texture2D> backBuffer;
    winrt::check_hresult(m_swapChain->GetBuffer(0, winrt::guid_of<ID3D11Texture2D>(), backBuffer.put_void()));
    winrt::check_hresult(m_d3dDevice->CreateRenderTargetView(backBuffer.get(), nullptr, m_renderTargetView.put()));
}

void FullscreenTransitionWindow::Flip()
{
    float color[4] = { 1.0f, 0.0f, 0.0f, 1.0f }; // RGBA
    m_d3dContext->ClearRenderTargetView(m_renderTargetView.get(), color);

    DXGI_PRESENT_PARAMETERS presentParameters{};
    winrt::check_hresult(m_swapChain->Present1(0, 0, &presentParameters));
}

void FullscreenTransitionWindow::Flip(winrt::Windows::UI::Color color)
{
    float colorf[4] = { color.R / 255.0f, color.G / 255.0f, color.B / 255.0f, color.A / 255.0f }; // RGBA
    m_d3dContext->ClearRenderTargetView(m_renderTargetView.get(), colorf);

    DXGI_PRESENT_PARAMETERS presentParameters{};
    winrt::check_hresult(m_swapChain->Present1(0, 0, &presentParameters));
}

LRESULT FullscreenTransitionWindow::MessageHandler(UINT const message, WPARAM const wparam, LPARAM const lparam)
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
    case WM_LBUTTONDBLCLK:
        if (m_mode == FullscreenTransitionTestMode::AdHoc)
        {
            Fullscreen(!m_fullscreen);
            Flip();
        }
        break;
    }

    return base_type::MessageHandler(message, wparam, lparam);
}