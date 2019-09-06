#pragma once

namespace wcliparse
{
    namespace impl
    {
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

        template <typename T>
        T GetFlagValueWithDefault(
            std::vector<std::wstring> const& args,
            std::wstring const& flag,
            std::wstring const& alias,
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

        template <>
        std::wstring GetFlagValueWithDefault(
            std::vector<std::wstring> const& args,
            std::wstring const& flag,
            std::wstring const& alias,
            std::wstring defaultValue)
        {
            auto value = GetFlagValue(args, flag);
            if (!value.empty())
            {
                return value;
            }
            value = GetFlagValue(args, alias);
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
            std::wstring const& alias,
            std::chrono::seconds defaultValue)
        {
            auto value = GetFlagValue(args, flag);
            if (!value.empty())
            {
                return std::chrono::seconds(std::stoi(value));
            }
            value = GetFlagValue(args, alias);
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

        std::wstring GetFlagValueEx(
            std::vector<std::wstring> const& args,
            std::wstring const& flag,
            std::wstring const& alias,
            std::wstring defaultValue)
        {
            std::wstring result;

            if (!defaultValue.empty())
            {
                if (!alias.empty())
                {
                    result = impl::GetFlagValueWithDefault(args, flag, alias, defaultValue);
                }
                else
                {
                    result = impl::GetFlagValueWithDefault(args, flag, defaultValue);
                }
            }
            else
            {
                if (!alias.empty())
                {
                    result = impl::GetFlagValue(args, flag, alias);
                }
                else
                {
                    result = impl::GetFlagValue(args, flag);
                }
            }

            return result;
        }

        bool GetFlagEx(
            std::vector<std::wstring> const& args,
            std::wstring const& flag,
            std::wstring const& alias)
        {
            bool result = false;

            if (!alias.empty())
            {
                result = impl::GetFlag(args, flag, alias);
            }
            else
            {
                result = impl::GetFlag(args, flag);
            }

            return result;
        }
    }

    class Argument
    {
    public:
        Argument(std::wstring const& name) { m_name = name; }

        Argument& Alias(std::wstring const& alias) { m_alias = alias; return *this; }
        Argument& Required(bool required) { m_required = required; return *this; }
        Argument& Description(std::wstring const& description) { m_description = description; return *this; }
        Argument& TakesValue(bool takesValue) { m_takesValue = takesValue; return *this; }
        Argument& DefaultValue(std::wstring const& defaultValue) { m_defaultValue = defaultValue; return *this; }

    public:
        std::wstring Name() { return m_name; }
        std::wstring Alias() { return m_alias; }
        bool Required() { return m_required; }
        std::wstring Description() { return m_description; }
        bool TakesValue() { return m_takesValue; }
        std::wstring DefaultValue() { return m_defaultValue; }

    private:
        std::wstring m_name;
        std::wstring m_alias;
        bool m_required = false;
        std::wstring m_description;
        bool m_takesValue = false;
        std::wstring m_defaultValue;
    };

    template <typename CommandValueT>
    class Command
    {
    public:
        Command(std::wstring const& name, CommandValueT value) { m_name = name; m_value = value; }

        Command& Argument(Argument argument) { m_arguments.push_back(argument); return *this; }

    public:
        std::wstring Name() { return m_name; }
        CommandValueT Value() { return m_value; }
        std::vector<wcliparse::Argument> Arguments() { return m_arguments; }

    private:
        std::wstring m_name;
        CommandValueT m_value;
        std::vector<wcliparse::Argument> m_arguments;
    };

    template <typename CommandValueT>
    class Matches
    {
    public:
        Matches(CommandValueT command, std::map<std::wstring, std::wstring> values) { m_command = command; m_values = values; }

        CommandValueT Command() { return m_command; }
        bool IsPresent(std::wstring const& name) { return m_values.find(name) != m_values.end(); }
        std::wstring ValueOf(std::wstring const& name) { return m_values[name]; }

    private:
        CommandValueT m_command;
        std::map<std::wstring, std::wstring> m_values;
    };

    template <typename CommandValueT>
    class Application
    {
    public:
        Application(std::wstring const& name) { m_name = name; }

        Application& Version(std::wstring const& version) { m_version = version; return *this; }
        Application& Author(std::wstring const& author) { m_author = author; return *this; }
        Application& About(std::wstring const& about) { m_about = about; return *this; }

        Application& Command(Command<CommandValueT> command) { m_commands.push_back(command); return *this; }

        Matches<CommandValueT> Parse(int argc, wchar_t* argv[])
        {
            std::vector<std::wstring> args(argv + 1, argv + argc);

            if (args.size() <= 0)
            {
                throw std::runtime_error("Invalid input!");
            }
            auto commandString = args[0];
            args.erase(args.begin());

            for (auto& command : m_commands)
            {
                if (command.Name() == commandString)
                {
                    std::map<std::wstring, std::wstring> values;
                    for (auto& argument : command.Arguments())
                    {
                        auto name = argument.Name();
                        auto alias = argument.Alias();
                        auto required = argument.Required();
                        bool flagPresent = false;
                        std::wstring value;

                        if (argument.TakesValue())
                        {
                            auto defaultValue = argument.DefaultValue();

                            value = impl::GetFlagValueEx(args, name, alias, defaultValue);

                            flagPresent = !value.empty();
                        }
                        else
                        {
                            flagPresent = impl::GetFlagEx(args, name, alias);
                        }

                        if (required && !flagPresent)
                        {
                            throw std::runtime_error("Invalid input!");
                        }

                        if (flagPresent)
                        {
                            values.insert({ name, value });
                        }
                    }
                    return Matches<CommandValueT>(command.Value(), values);
                }
            }

            throw std::runtime_error("Invalid input!");
        }

        void PrintUsage()
        {
            std::wcout << m_name.c_str() << L" usage:" << std::endl;
            std::wcout << L"\t" << L"<command> <flags...>" << std::endl;
            std::wcout << std::endl;
            for(auto& command : m_commands)
            {
                std::wcout << L"\t" << command.Name();
                auto args = command.Arguments();
                if (args.size() > 0)
                {
                    std::wcout << L"\t";
                    for (auto& argument : args)
                    {
                        std::wcout << argument.Name().c_str() << L" ";
                        auto alias = argument.Alias();
                        if (!alias.empty())
                        {
                            std::wcout << L"(" << alias.c_str() << L") ";
                        }

                        auto required = argument.Required();
                        auto desc = argument.Description();
                        std::wcout << L"<";
                        if (!desc.empty())
                        {
                            std::wcout << desc.c_str() << L", ";
                        }
                        std::wcout << (required ? L"required" : L"optional");
                        auto defaultValue = argument.DefaultValue();
                        if (!defaultValue.empty())
                        {
                            std::wcout << L", default " << defaultValue.c_str();
                        }
                        std::wcout << L"> ";
                    }
                }
                std::wcout << std::endl;
            }
        }

    private:
        std::wstring m_name;
        std::wstring m_version;
        std::wstring m_author;
        std::wstring m_about;
        std::vector<wcliparse::Command<CommandValueT>> m_commands;
    };
}