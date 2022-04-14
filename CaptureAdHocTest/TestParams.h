#pragma once

namespace testparams
{
    enum class FullscreenMode
    {
        SetFullscreenState,
        FullscreenWindow
    };
    enum class FullscreenTransitionTestMode
    {
        AdHoc,
        Automated
    };
    enum class DisplayAffinityMode
    {
        None,
        Monitor,
        ExcludeFromCapture
    };
    enum class WindowStyleTestMode
    {
        AdHoc,
        Automated
    };
    enum class MarginsTestMode
    {
        AdHoc,
        Automated
    };

    struct Alpha {};
    struct FullscreenRate
    {
        FullscreenMode FullscreenMode = FullscreenMode::SetFullscreenState;
    };
    struct FullscreenTransition
    {
        FullscreenTransitionTestMode TransitionMode = FullscreenTransitionTestMode::AdHoc;
    };
    struct WindowRate
    {
        std::wstring WindowTitle;
        std::chrono::seconds Delay = std::chrono::seconds(0);
        std::chrono::seconds Duration = std::chrono::seconds(10);
    };
    struct CursorDisable
    {
        bool Monitor = false;
        bool Window = false;
    };
    struct HDRContent {};
    struct DisplayAffinity
    {
        DisplayAffinityMode Mode = DisplayAffinityMode::None;
    };
    struct WindowStyle
    {
        WindowStyleTestMode TransitionMode = WindowStyleTestMode::AdHoc;
    };
    struct WindowMargins
    {
        MarginsTestMode TestMode = MarginsTestMode::AdHoc;
    };
    struct MonitorOff {};
    struct PCInfo {};
    struct MonitorInfo {};

    typedef std::variant<
        Alpha,
        FullscreenRate,
        FullscreenTransition,
        WindowRate,
        CursorDisable,
        HDRContent,
        DisplayAffinity,
        WindowStyle,
        WindowMargins,
        MonitorOff,
        PCInfo,
        MonitorInfo
    > TestParams;
};