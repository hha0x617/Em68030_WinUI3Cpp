#pragma once

#include "AboutDialog.g.h"

namespace winrt::Em68030::implementation
{
    struct AboutDialog : AboutDialogT<AboutDialog>
    {
        AboutDialog();
    };
}

namespace winrt::Em68030::factory_implementation
{
    struct AboutDialog : AboutDialogT<AboutDialog, implementation::AboutDialog>
    {
    };
}
