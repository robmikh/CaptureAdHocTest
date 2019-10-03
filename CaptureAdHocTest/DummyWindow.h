#pragma once
#include "DesktopWindow.h"

struct DummyWindow : DesktopWindow<DummyWindow>
{
	static const std::wstring ClassName;
    static void RegisterWindowClass();
    DummyWindow(std::wstring const& titleString);
    LRESULT MessageHandler(UINT const message, WPARAM const wparam, LPARAM const lparam);
};