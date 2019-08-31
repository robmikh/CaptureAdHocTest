#pragma once
#include "DesktopWindow.h"

struct DummyWindow : DesktopWindow<DummyWindow>
{
    static void RegisterWindowClass();
    DummyWindow();
    LRESULT MessageHandler(UINT const message, WPARAM const wparam, LPARAM const lparam);
};