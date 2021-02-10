#include "pch.h"
#include "MarginsWindow.h"
#include "ControlsHelper.h"
#include "testutils.h"

namespace winrt
{
    using namespace Windows::UI;
}

const std::wstring MarginsWindow::ClassName = L"CaptureAdHocTest.MarginsWindow";

void MarginsWindow::RegisterWindowClass()
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

MarginsWindow::MarginsWindow(std::wstring const& title, winrt::Color const& backgroundColor, uint32_t width, uint32_t height, bool showControls)
{
    auto instance = winrt::check_pointer(GetModuleHandleW(nullptr));
    m_windowClosed = wil::shared_event(wil::EventOptions::None);
    SetBackgroundColor(backgroundColor);

    m_initialWidth = width;
    m_initialHeight = height;

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

MarginsWindow::~MarginsWindow()
{
}

void MarginsWindow::SetBackgroundColor(winrt::Color const& color)
{
    m_brush.reset(winrt::check_pointer(CreateSolidBrush(RGB(color.R, color.G, color.B))));
    winrt::check_bool(InvalidateRect(m_window, nullptr, false));
}

LRESULT MarginsWindow::MessageHandler(UINT const message, WPARAM const wparam, LPARAM const lparam)
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
            if (hwnd == m_toggleFullscreenButton)
            {
                Fullscreen(!m_fullscreen);
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

void MarginsWindow::CreateControls()
{
    auto controls = StackPanel(m_window, 10, 10, 200);

    m_toggleFullscreenButton = controls.CreateControl(ControlType::Button, L"Toggle fullscreen");
}

void MarginsWindow::Fullscreen(bool isFullscreen)
{
    if (m_fullscreen != isFullscreen)
    {
        m_fullscreen = isFullscreen;
        auto currentStyle = GetWindowLongW(m_window, GWL_STYLE);
        auto currentExStyle = GetWindowLongW(m_window, GWL_EXSTYLE);
        auto width = 0;
        auto height = 0;
        auto x = 0;
        auto y = 0;

        auto monitor = MonitorFromWindow(m_window, MONITOR_DEFAULTTOPRIMARY);
        auto monitorInfo = CreateWin32Struct<MONITORINFO>();
        winrt::check_bool(GetMonitorInfoW(monitor, &monitorInfo));
        auto workAreaWidth = monitorInfo.rcWork.right - monitorInfo.rcWork.left;
        auto workAreaHeight = monitorInfo.rcWork.bottom - monitorInfo.rcWork.top;

        if (m_fullscreen)
        {
            currentStyle &= ~(WS_CAPTION | WS_THICKFRAME | WS_MINIMIZEBOX | WS_MAXIMIZEBOX | WS_SYSMENU);
            currentExStyle &= ~(WS_EX_DLGMODALFRAME | WS_EX_CLIENTEDGE | WS_EX_STATICEDGE);
            currentExStyle |= WS_EX_TOPMOST;

            x = monitorInfo.rcWork.left;
            y = monitorInfo.rcWork.top;
            width = workAreaWidth;
            height = workAreaHeight;
        }
        else 
        {
            currentStyle = WS_OVERLAPPEDWINDOW;
            currentExStyle = 0;
            x = (workAreaWidth - m_initialWidth) / 2;
            y = (workAreaHeight - m_initialHeight) / 2;
            width = m_initialWidth;
            height = m_initialHeight;
        }

        SetWindowLongW(m_window, GWL_STYLE, currentStyle | WS_VISIBLE);
        SetWindowLongW(m_window, GWL_EXSTYLE, currentExStyle);
        winrt::check_bool(SetWindowPos(m_window, nullptr, x, y, width, height, SWP_FRAMECHANGED | SWP_SHOWWINDOW));
        ShowWindow(m_window, SW_SHOW);
    }
}