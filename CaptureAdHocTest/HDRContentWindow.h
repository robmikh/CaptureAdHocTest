#pragma once
#include "DesktopWindow.h"

struct HDRContentWindow : DesktopWindow<HDRContentWindow>
{
    static const std::wstring ClassName;
    static void RegisterWindowClass();

    HDRContentWindow(winrt::Windows::UI::Composition::Compositor const& compositor, winrt::com_ptr<ID2D1Device> const& d2dDevice);
    ~HDRContentWindow();

    LRESULT MessageHandler(UINT const message, WPARAM const wparam, LPARAM const lparam);

    wil::shared_event Closed() { return m_windowClosed; }

private:
    void CloseWindow() { m_windowClosed.SetEvent(); }
private:
    winrt::com_ptr<ID2D1Device> m_d2dDevice;
    winrt::Windows::UI::Composition::Compositor m_compositor{ nullptr };
    winrt::Windows::UI::Composition::Desktop::DesktopWindowTarget m_target{ nullptr };
    winrt::Windows::UI::Composition::CompositionGraphicsDevice m_compositionGraphics{ nullptr };
    wil::shared_event m_windowClosed{ nullptr };
};