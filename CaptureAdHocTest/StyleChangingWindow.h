#pragma once
#include <robmikh.common/DesktopWindow.h>

enum class WindowStyle
{
    Overlapped,
    Popup
};

struct StyleChangingWindow : robmikh::common::desktop::DesktopWindow<StyleChangingWindow>
{
    static const std::wstring ClassName;
    static void RegisterWindowClass();

    StyleChangingWindow(std::wstring const& title, winrt::Windows::UI::Color const& backgroundColor, uint32_t width, uint32_t height, bool showControls);
    ~StyleChangingWindow();

    LRESULT MessageHandler(UINT const message, WPARAM const wparam, LPARAM const lparam);

    void SetBackgroundColor(winrt::Windows::UI::Color const& color);

    WindowStyle Style() { return m_style; }
    void Style(WindowStyle const& style) { m_style = style; OnStyleChange(); }
    wil::shared_event Closed() { return m_windowClosed; }

private:
    void CloseWindow() { m_windowClosed.SetEvent(); }
    void CreateControls(HINSTANCE instance);
    void OnStyleChangeButtonPressed();
    void OnStyleChange();

private:
    wil::shared_event m_windowClosed{ nullptr };
    WindowStyle m_style = WindowStyle::Overlapped;
    HWND m_changeStyleButton = nullptr;
    wil::unique_hbrush m_brush;
};