#pragma once
#include "DesktopWindow.h"

struct FullScreenMaxRateWindow : DesktopWindow<FullScreenMaxRateWindow>
{
    static void RegisterWindowClass();

    FullScreenMaxRateWindow();

    LRESULT MessageHandler(UINT const message, WPARAM const wparam, LPARAM const lparam);

    void Flip();
    bool Closed() { return m_windowClosed; }

private:
    winrt::com_ptr<ID3D11Device> m_d3dDevice;
    winrt::com_ptr<IDXGISwapChain1> m_swapChain;
    winrt::com_ptr<ID3D11DeviceContext> m_d3dContext;
    winrt::com_ptr<ID3D11RenderTargetView> m_renderTargetView;
    bool m_windowClosed = false;
};