#pragma once
#include <winrt/Microsoft.Windows.ApplicationModel.Resources.h>
#include <string>
#include <sstream>
#include <type_traits>

namespace Em68030
{
    class ResourceHelper
    {
    public:
        /// Load a localized string by resource key.
        static winrt::hstring GetString(const wchar_t* key)
        {
            return GetLoader().GetString(key);
        }

        /// Load a localized string by resource key, returned as std::wstring.
        static std::wstring GetStdString(const wchar_t* key)
        {
            return std::wstring(GetString(key));
        }

        /// Load a format string by resource key, then replace {0}, {1}, ... with the given arguments.
        template<typename... Args>
        static std::wstring Format(const wchar_t* key, Args&&... args)
        {
            auto fmt = GetStdString(key);
            return FormatString(fmt, std::forward<Args>(args)...);
        }

        /// Replace {0}, {1}, ... in a format string with the given arguments.
        template<typename... Args>
        static std::wstring FormatString(const std::wstring& fmt, Args&&... args)
        {
            std::wstring result = fmt;
            int index = 0;
            (ReplacePlaceholder(result, index++, std::forward<Args>(args)), ...);
            return result;
        }

    private:
        static winrt::Microsoft::Windows::ApplicationModel::Resources::ResourceLoader& GetLoader()
        {
            static winrt::Microsoft::Windows::ApplicationModel::Resources::ResourceLoader loader;
            return loader;
        }

        // Convert a value to wstring for placeholder replacement.
        static std::wstring ToWString(const std::wstring& value) { return value; }
        static std::wstring ToWString(const wchar_t* value) { return std::wstring(value); }
        static std::wstring ToWString(std::wstring_view value) { return std::wstring(value); }
        static std::wstring ToWString(const winrt::hstring& value) { return std::wstring(value); }

        static std::wstring ToWString(const std::string& value)
        {
            return std::wstring(value.begin(), value.end());
        }

        static std::wstring ToWString(const char* value)
        {
            std::string s(value);
            return std::wstring(s.begin(), s.end());
        }

        // Numeric types
        static std::wstring ToWString(int value) { return std::to_wstring(value); }
        static std::wstring ToWString(unsigned int value) { return std::to_wstring(value); }
        static std::wstring ToWString(long value) { return std::to_wstring(value); }
        static std::wstring ToWString(unsigned long value) { return std::to_wstring(value); }
        static std::wstring ToWString(long long value) { return std::to_wstring(value); }
        static std::wstring ToWString(unsigned long long value) { return std::to_wstring(value); }

        static std::wstring ToWString(float value)
        {
            std::wostringstream oss;
            oss << value;
            return oss.str();
        }

        static std::wstring ToWString(double value)
        {
            std::wostringstream oss;
            oss << value;
            return oss.str();
        }

        // Replace {N} in the string with the converted value.
        template<typename T>
        static void ReplacePlaceholder(std::wstring& str, int index, T&& value)
        {
            std::wstring placeholder = L"{" + std::to_wstring(index) + L"}";
            std::wstring replacement = ToWString(std::forward<T>(value));
            size_t pos = 0;
            while ((pos = str.find(placeholder, pos)) != std::wstring::npos)
            {
                str.replace(pos, placeholder.length(), replacement);
                pos += replacement.length();
            }
        }
    };
}
