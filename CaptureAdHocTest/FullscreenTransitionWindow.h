#pragma once
#include <robmikh.common/DesktopWindow.h>
#include "TestParams.h"

struct FullscreenTransitionWindow : robmikh::common::desktop::DesktopWindow<FullscreenTransitionWindow>
{
    static const std::wstring ClassName;
    static void RegisterWindowClass();

    FullscreenTransitionWindow(testparams::FullscreenTransitionTestMode mode);
    ~FullscreenTransitionWindow();

    LRESULT MessageHandler(UINT const message, WPARAM const wparam, LPARAM const lparam);

    void Flip();
    void Flip(winrt::Windows::UI::Color color);
    bool Fullscreen() { return m_fullscreen; }
    void Fullscreen(bool isFullscreen);
    wil::shared_event Closed() { return m_windowClosed; }

private:
    void CloseWindow() { m_windowClosed.SetEvent(); }

private:
    testparams::FullscreenTransitionTestMode m_mode;
    winrt::com_ptr<ID3D11Device> m_d3dDevice;
    winrt::com_ptr<IDXGISwapChain1> m_swapChain;
    winrt::com_ptr<ID3D11DeviceContext> m_d3dContext;
    winrt::com_ptr<ID3D11RenderTargetView> m_renderTargetView;
    wil::shared_event m_windowClosed{ nullptr };
    bool m_fullscreen = false;
};