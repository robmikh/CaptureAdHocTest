#include "pch.h"
#include "DummyWindow.h"

const std::wstring DummyWindow::ClassName = L"DummyWindow";

void DummyWindow::RegisterWindowClass()
{
    auto instance = winrt::check_pointer(GetModuleHandleW(nullptr));
    WNDCLASSEX wcex = { sizeof(wcex) };
    wcex.style = CS_HREDRAW | CS_VREDRAW;
    wcex.lpfnWndProc = WndProc;
    wcex.hInstance = instance;
    wcex.hIcon = LoadIconW(instance, IDI_APPLICATION);
    wcex.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    wcex.hbrBackground = nullptr;
    wcex.lpszClassName = ClassName.c_str();
    wcex.hIconSm = LoadIconW(instance, IDI_APPLICATION);
    winrt::check_bool(RegisterClassExW(&wcex));
}

DummyWindow::DummyWindow(std::wstring const& titleString)
{
    auto instance = winrt::check_pointer(GetModuleHandleW(nullptr));
	m_windowClosed = wil::shared_event(wil::EventOptions::None);

    winrt::check_bool(CreateWindowExW(WS_EX_NOREDIRECTIONBITMAP, ClassName.c_str(), titleString.c_str(), WS_OVERLAPPEDWINDOW,
        CW_USEDEFAULT, CW_USEDEFAULT, 400, 400, nullptr, nullptr, instance, this));
    WINRT_ASSERT(m_window);

    ShowWindow(m_window, SW_SHOWDEFAULT);
    UpdateWindow(m_window);
}

LRESULT DummyWindow::MessageHandler(UINT const message, WPARAM const wparam, LPARAM const lparam)
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
	}

	return base_type::MessageHandler(message, wparam, lparam);
}