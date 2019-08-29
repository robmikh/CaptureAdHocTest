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
    std::wcout << L"\ttest\t--id <test id, required> --window <only on Window Capture Rate test>" << std::endl;
    std::wcout << std::endl;
    std::wcout << L"\tTest ids:" << std::endl;
    std::wcout << L"\t\tTransparency = 0" << std::endl;
    std::wcout << L"\t\tMaxCaptureRate = 1" << std::endl;
    std::wcout << L"\t\tWindow Capture Rate = 2" << std::endl;
}