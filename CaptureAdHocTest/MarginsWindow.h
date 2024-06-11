#pragma once
#include <robmikh.common/DesktopWindow.h>

struct MarginsWindow : robmikh::common::desktop::DesktopWindow<MarginsWindow>
{
    static const std::wstring ClassName;
    static void RegisterWindowClass();

    MarginsWindow(std::wstring const& title, winrt::Windows::UI::Color const& backgroundColor, uint32_t width, uint32_t height, bool showControls);
    ~MarginsWindow();

    LRESULT MessageHandler(UINT const message, WPARAM const wparam, LPARAM const lparam);

    void SetBackgroundColor(winrt::Windows::UI::Color const& color);
    bool Fullscreen() { return m_fullscreen; }
    void Fullscreen(bool isFullscreen);
    wil::shared_event Closed() { return m_windowClosed; }

private:
    void CloseWindow() { m_windowClosed.SetEvent(); }
    void CreateControls(HINSTANCE instance);
    winrt::fire_and_forget TakeSnapshot();

private:
    wil::shared_event m_windowClosed{ nullptr };
    bool m_fullscreen = false;
    HWND m_toggleFullscreenButton = nullptr;
    HWND m_takeSnapshotButton = nullptr;
    wil::unique_hbrush m_brush;
    uint32_t m_initialWidth;
    uint32_t m_initialHeight;
};