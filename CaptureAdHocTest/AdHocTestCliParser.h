#pragma once
#include <robmikh.common/wcliparse.h>
#include "TestParams.h"

typedef std::function<testparams::TestParams(robmikh::common::wcli::Matches&)> TestParamInputValidator;

class AdHocTestCliValidator
{
public:
    static testparams::TestParams ValidateFullscreenRate(robmikh::common::wcli::Matches& matches)
    {
        auto setFullscreenState = matches.IsPresent(L"--setfullscreenstate");
        auto fullscreenWindow = matches.IsPresent(L"--fullscreenwindow");
        if (setFullscreenState == fullscreenWindow)
        {
            throw std::runtime_error("Strictly one fullscreen mode required!");
        }

        return testparams::TestParams(testparams::FullscreenRate
        {
            setFullscreenState ? testparams::FullscreenMode::SetFullscreenState : testparams::FullscreenMode::FullscreenWindow
        });
    }

    static testparams::TestParams ValidateFullscreenTransition(robmikh::common::wcli::Matches& matches)
    {
        auto adHocMode = matches.IsPresent(L"--adhoc");
        auto automatedMode = matches.IsPresent(L"--automated");
        if (adHocMode == automatedMode)
        {
            throw std::runtime_error("Strictly one test mode required!");
        }

        return testparams::TestParams(testparams::FullscreenTransition
        {
            adHocMode ? testparams::FullscreenTransitionTestMode::AdHoc : testparams::FullscreenTransitionTestMode::Automated
        });
    }

    static testparams::TestParams ValidateWindowRate(robmikh::common::wcli::Matches& matches)
    {
        auto result = testparams::WindowRate();
        result.WindowTitle = matches.ValueOf(L"--window");

        if (matches.IsPresent(L"--delay"))
        {
            auto delayString = matches.ValueOf(L"--delay");
            result.Delay = std::chrono::seconds(std::stoi(delayString));
        }

        if (matches.IsPresent(L"--duration"))
        {
            auto durationString = matches.ValueOf(L"--duration");
            result.Duration = std::chrono::seconds(std::stoi(durationString));
        }

        return testparams::TestParams(result);
    }

    static testparams::TestParams ValidateCursorDisable(robmikh::common::wcli::Matches& matches)
    {
        auto monitor = matches.IsPresent(L"--monitor");
        auto window = matches.IsPresent(L"--window");
        if (!monitor && !window)
        {
            throw std::runtime_error("Strictly one test mode required!");
        }

        return testparams::TestParams(testparams::CursorDisable{ monitor, window });
    }

    static testparams::TestParams ValidateDisplayAffinity(robmikh::common::wcli::Matches& matches)
    {
        auto none = matches.IsPresent(L"--none");
        auto monitor = matches.IsPresent(L"--monitor");
        auto exclude = matches.IsPresent(L"--exclude");
        if (!none && !monitor && !exclude)
        {
            throw std::runtime_error("Strictly one display mode required!");
        }

        auto mode = testparams::DisplayAffinityMode::None;
        if (monitor)
        {
            mode = testparams::DisplayAffinityMode::Monitor;
        }
        else if (exclude)
        {
            mode = testparams::DisplayAffinityMode::ExcludeFromCapture;
        }

        return testparams::TestParams(testparams::DisplayAffinity{ mode });
    }

    static testparams::TestParams ValidateWindowStyle(robmikh::common::wcli::Matches& matches)
    {
        auto adHocMode = matches.IsPresent(L"--adhoc");
        auto automatedMode = matches.IsPresent(L"--automated");
        if (adHocMode == automatedMode)
        {
            throw std::runtime_error("Strictly one test mode required!");
        }

        return testparams::TestParams(testparams::WindowStyle
        {
            adHocMode ? testparams::WindowStyleTestMode::AdHoc : testparams::WindowStyleTestMode::Automated
        });
    }

    static testparams::TestParams ValidateWindowMargins(robmikh::common::wcli::Matches& matches)
    {
        auto adHocMode = matches.IsPresent(L"--adhoc");
        auto automatedMode = matches.IsPresent(L"--automated");
        if (adHocMode == automatedMode)
        {
            throw std::runtime_error("Strictly one test mode required!");
        }

        return testparams::TestParams(testparams::WindowMargins
            {
                adHocMode ? testparams::MarginsTestMode::AdHoc : testparams::MarginsTestMode::Automated
            });
    }

private:
    AdHocTestCliValidator() {}
};
