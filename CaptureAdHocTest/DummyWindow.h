#pragma once
#include <robmikh.common/DesktopWindow.h>

struct DummyWindow : robmikh::common::desktop::DesktopWindow<DummyWindow>
{
	static const std::wstring ClassName;
    static void RegisterWindowClass();
    DummyWindow(std::wstring const& titleString);
    LRESULT MessageHandler(UINT const message, WPARAM const wparam, LPARAM const lparam);

	wil::shared_event Closed() { return m_windowClosed; }

private:
	void CloseWindow() { m_windowClosed.SetEvent(); }
private:
	wil::shared_event m_windowClosed{ nullptr };
};