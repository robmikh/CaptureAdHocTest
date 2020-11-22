#include "pch.h"
#include "StyleChangingWindow.h"
#include "ControlsHelper.h"

namespace winrt
{
    using namespace Windows::UI;
}

const std::wstring StyleChangingWindow::ClassName = L"WindowStyleCaptureRepro.StyleChangingWindow";

void StyleChangingWindow::RegisterWindowClass()
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

StyleChangingWindow::StyleChangingWindow(std::wstring const& title, winrt::Color const& backgroundColor, uint32_t width, uint32_t height, bool showControls)
{
    auto instance = winrt::check_pointer(GetModuleHandleW(nullptr));
    m_windowClosed = wil::shared_event(wil::EventOptions::None);
    SetBackgroundColor(backgroundColor);

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

StyleChangingWindow::~StyleChangingWindow()
{
}

void StyleChangingWindow::SetBackgroundColor(winrt::Color const& color)
{
    m_brush.reset(winrt::check_pointer(CreateSolidBrush(RGB(color.R, color.G, color.B))));
    winrt::check_bool(InvalidateRect(m_window, nullptr, false));
}

struct PaintHelper
{
    PaintHelper(HWND window)
    {
        m_window = window;
        m_hdc = winrt::check_pointer(BeginPaint(m_window, &m_paint));
    }

    ~PaintHelper()
    {
        EndPaint(m_window, &m_paint);
    }

    ::HDC HDC() { return m_hdc; }
    PAINTSTRUCT const& PaintStruct() { return m_paint; }

private:
    HWND m_window = nullptr;
    PAINTSTRUCT m_paint = {};
    ::HDC m_hdc = nullptr;
};

LRESULT StyleChangingWindow::MessageHandler(UINT const message, WPARAM const wparam, LPARAM const lparam)
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
            if (hwnd == m_changeStyleButton)
            {
                OnStyleChangeButtonPressed();
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

void StyleChangingWindow::CreateControls()
{
    auto controls = StackPanel(m_window, 10, 10, 200);

    m_changeStyleButton = controls.CreateControl(ControlType::Button, L"Change Style");
}

WindowStyle CycleStyle(WindowStyle style)
{
    switch (style)
    {
    case WindowStyle::Overlapped:
        return WindowStyle::Popup;
    case WindowStyle::Popup:
        return WindowStyle::Overlapped;
    }
}

LONG GetStyleFromWindowdStyle(WindowStyle style)
{
    switch (style)
    {
    case WindowStyle::Overlapped:
        return WS_OVERLAPPEDWINDOW;
    case WindowStyle::Popup:
        return WS_POPUP;
    }
}

void ApplyStyle(HWND window, WindowStyle style)
{
    auto newStyle = GetStyleFromWindowdStyle(style);
    SetWindowLongW(window, GWL_STYLE, newStyle);
    winrt::check_bool(SetWindowPos(window, nullptr, 0, 0, 0, 0, SWP_FRAMECHANGED | SWP_NOMOVE | SWP_NOSIZE));
    ShowWindow(window, SW_SHOW);
}

void StyleChangingWindow::OnStyleChangeButtonPressed()
{
    auto newStyle = CycleStyle(m_style);
    ApplyStyle(m_window, newStyle);
    m_style = newStyle;
}

void StyleChangingWindow::OnStyleChange()
{
    ApplyStyle(m_window, m_style);
}