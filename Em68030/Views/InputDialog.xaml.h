#pragma once

#include "InputDialog.g.h"

namespace winrt::Em68030::implementation
{
    struct InputDialog : InputDialogT<InputDialog>
    {
        InputDialog();

        /// Show the dialog with the given title, prompt, default value, and parent UIElement.
        /// Returns the entered text, or an empty string if the user cancelled.
        Windows::Foundation::IAsyncOperation<winrt::hstring>
            ShowAsync(winrt::hstring title, winrt::hstring prompt,
                      winrt::hstring defaultValue, Microsoft::UI::Xaml::UIElement const& parent);

        /// Get the text entered by the user.
        winrt::hstring InputText();

    private:
        // Nothing extra needed
    };
}

namespace winrt::Em68030::factory_implementation
{
    struct InputDialog : InputDialogT<InputDialog, implementation::InputDialog>
    {
    };
}
