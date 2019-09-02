#pragma once

std::wstring GetFlagValue(
    std::vector<std::wstring> const& args,
    std::wstring const& flag)
{
    auto it = std::find(args.begin(), args.end(), flag);
    if (it != args.end() && ++it != args.end())
    {
        return *it;
    }
    return std::wstring();
}

std::wstring GetFlagValue(
    std::vector<std::wstring> const& args,
    std::wstring const& flag,
    std::wstring const& alias)
{
    auto flagResult = GetFlagValue(args, flag);
    if (!flagResult.empty())
    {
        return flagResult;
    }
    auto aliasResult = GetFlagValue(args, alias);
    return aliasResult;
}

template <typename T>
T GetFlagValueWithDefault(
    std::vector<std::wstring> const& args,
    std::wstring const& flag,
    T defaultValue);

template <>
std::wstring GetFlagValueWithDefault(
    std::vector<std::wstring> const& args,
    std::wstring const& flag,
    std::wstring defaultValue)
{
    auto value = GetFlagValue(args, flag);
    if (!value.empty())
    {
        return value;
    }
    return defaultValue;
}

template <>
std::chrono::seconds GetFlagValueWithDefault(
    std::vector<std::wstring> const& args,
    std::wstring const& flag,
    std::chrono::seconds defaultValue)
{
    auto value = GetFlagValue(args, flag);
    if (!value.empty())
    {
        return std::chrono::seconds(std::stoi(value));
    }
    return defaultValue;
}

bool GetFlag(
    std::vector<std::wstring> const& args,
    std::wstring const& flag)
{
    auto it = std::find(args.begin(), args.end(), flag);
    return it != args.end();
}

bool GetFlag(
    std::vector<std::wstring> const& args,
    std::wstring const& flag,
    std::wstring const& alias)
{
    return GetFlag(args, flag) || GetFlag(args, alias);
}

void PrintUsage()
{
    std::wcout << L"CaptureAdHocTest.exe usage:" << std::endl;
    std::wcout << L"\t<command> <flags...>" << std::endl;
    std::wcout << std::endl;
    std::wcout << L"\t" << L"alpha" << std::endl;
    std::wcout << L"\t" << L"fullscreen-rate"<< L"\t" << L"[ --setfullscreenstate (-sfs) || --fullscreenwindow (-fw) ]" << std::endl;
    std::wcout << L"\t" << L"window-rate" << L"\t" << L"--window <window title string, required> --delay <seconds, optional> --duration <seconds, optional, default 10s>" << std::endl;
    std::wcout << L"\t" << L"pc-info" << std::endl;
}