# Internationalization (i18n)

Em68030 (C++/WinUI3) supports multiple UI languages via Windows App SDK resource files (`.resw`).
The application automatically selects the language based on the OS locale at startup.

## Supported Languages

| Language | Culture | Resource File |
|----------|---------|---------------|
| English (default) | `en-US` | `Strings/en-US/Resources.resw` |
| Japanese | `ja-JP` | `Strings/ja-JP/Resources.resw` |

## Architecture

- **`Strings/{locale}/Resources.resw`** — Localized string resources per language. The `.resw` format is identical to `.resx` XML format.
- **`Helpers/ResourceHelper.h`** — Header-only helper class using `Microsoft::Windows::ApplicationModel::Resources::ResourceLoader`.
- **XAML localization** — Elements use `x:Uid` attribute. The resource key format is `UidName.Property` (e.g., `BtnRun.Content`, `MenuFile.Title`).
- **Code-behind localization** — Dynamic strings use `ResourceHelper::GetString()` and `ResourceHelper::Format()`.

### ResourceHelper API

```cpp
#include "Helpers/ResourceHelper.h"
using R = Em68030::ResourceHelper;

// Simple string lookup
winrt::hstring text = R::GetString(L"Menu_File");

// As std::wstring
std::wstring text = R::GetStdString(L"Toolbar_Run");

// Format string with placeholders {0}, {1}, ...
std::wstring msg = R::Format(L"Status_FileFormat", filename);
std::wstring perf = R::Format(L"Status_MhzFormat", mhzStr, mipsStr);
```

## Strings NOT Localized

The following are kept in their original form regardless of locale:

- Register names: D0–D7, A0–A7, PC, SR, SSP, VBR, FP0–FP7, CR, IAR
- Flag names: X, N, Z, V, C, S, T
- Board/OS identifiers: MVME147, Generic, NetBSD, Linux
- Technical terms in status bar: JIT, MIPS, MHz
- Diagnostic/trace messages (`[EMU] ...`) — developer-facing
- URLs, version numbers

## Adding a New String

1. **Add the key/value to `Strings/en-US/Resources.resw`** (English).

2. **Add the same key with a translated value to each locale file** (e.g., `Strings/ja-JP/Resources.resw`).

3. **For XAML elements**, use `x:Uid`:
   ```xml
   <TextBlock x:Uid="MyNewLabel" />
   ```
   And add a key `MyNewLabel.Text` to the `.resw` files:
   ```xml
   <data name="MyNewLabel.Text" xml:space="preserve">
     <value>My Label Text</value>
   </data>
   ```
   The `x:Uid` system automatically maps `UidName.PropertyName` keys to element properties.

   Common property suffixes:
   | Element | Property Key Suffix |
   |---------|-------------------|
   | `TextBlock` | `.Text` |
   | `Button` | `.Content` |
   | `MenuBarItem` | `.Title` |
   | `MenuFlyoutItem` | `.Text` |
   | `ToggleButton` | `.Content` |
   | `CheckBox` | `.Content` |
   | `ContentDialog` | `.Title`, `.PrimaryButtonText`, `.CloseButtonText` |
   | `ToolTip` | `.Content` |

4. **For code-behind strings**, use `ResourceHelper`:
   ```cpp
   #include "Helpers/ResourceHelper.h"
   // Simple key (no x:Uid suffix needed)
   auto text = Em68030::ResourceHelper::GetString(L"Msg_Error");
   // Format string
   auto msg = Em68030::ResourceHelper::Format(L"Status_FileFormat", filename);
   ```

5. **Build and verify** — MakePRI compiles `.resw` files into `resources.pri` during build.

## Adding a New Language

1. **Create a new directory** `Strings/{locale}/` where `{locale}` is a BCP 47 language tag:
   - `Strings/de-DE/Resources.resw` — German
   - `Strings/zh-Hans/Resources.resw` — Simplified Chinese
   - `Strings/ko-KR/Resources.resw` — Korean

2. **Copy `Strings/en-US/Resources.resw`** to the new directory and translate all `<value>` elements. All keys must match exactly.

3. **Add the new `.resw` to `Em68030.vcxproj`**:
   ```xml
   <ItemGroup>
     <PRIResource Include="Strings\en-US\Resources.resw" />
     <PRIResource Include="Strings\ja-JP\Resources.resw" />
     <PRIResource Include="Strings\de-DE\Resources.resw" />  <!-- New -->
   </ItemGroup>
   ```

4. **Build** — MakePRI will include the new language in `resources.pri`.

5. **Verify** — Set your OS display language to the new locale (or use the forced locale method below) and launch the application.

## Forcing a Specific Locale (Debugging / Layout Testing)

### Method 1: Override in ResourceHelper

Temporarily modify `ResourceHelper::GetLoader()` to specify a language:

```cpp
static winrt::Microsoft::Windows::ApplicationModel::Resources::ResourceLoader& GetLoader()
{
    static winrt::Microsoft::Windows::ApplicationModel::Resources::ResourceManager resMgr;
    // Force English
    static auto context = resMgr.CreateResourceContext();
    context.QualifierValues().Insert(L"Language", L"en-US");
    // context.QualifierValues().Insert(L"Language", L"ja-JP");  // Force Japanese

    static winrt::Microsoft::Windows::ApplicationModel::Resources::ResourceLoader loader;
    return loader;
}
```

Or use `ResourceManager` with a custom `ResourceContext` directly:

```cpp
#include <winrt/Microsoft.Windows.ApplicationModel.Resources.h>

auto resMgr = winrt::Microsoft::Windows::ApplicationModel::Resources::ResourceManager();
auto context = resMgr.CreateResourceContext();
context.QualifierValues().Insert(L"Language", L"en-US");
auto candidate = resMgr.MainResourceMap().GetValue(L"Resources/Menu_File", context);
auto text = candidate.ValueAsString();
```

### Method 2: Windows Preferred Language

Change the Windows preferred language order in Settings > Time & Language > Language & region. Move the desired language to the top of the list. This affects all applications.

### Method 3: Per-Process Language Override

Use the `SetThreadPreferredUILanguages` Windows API at the start of `main()` or `wWinMain()`:

```cpp
#include <windows.h>
#include <winnls.h>

// Force English at process startup
ULONG numLanguages = 0;
SetThreadPreferredUILanguages(MUI_LANGUAGE_NAME, L"en-US\0", &numLanguages);
```

**Important:** Remove or comment out any forced locale code before committing. It is intended for debugging only.

## Resource Key Naming Convention

| Prefix | Usage | Example |
|--------|-------|---------|
| `Menu_` | Menu items | `Menu_File`, `Menu_OpenBinary` |
| `Toolbar_` | Toolbar buttons | `Toolbar_Run`, `Toolbar_Stop` |
| `Disasm_` | Disassembly pane | `Disasm_Title`, `Disasm_Go` |
| `Regs_` | Register pane | `Regs_Title`, `Regs_Edit` |
| `MemDump_` | Memory dump pane | `MemDump_Title`, `MemDump_Go` |
| `Context_` | Context menu items | `Context_Copy`, `Context_Paste` |
| `Status_` | Status bar text | `Status_Running`, `Status_MhzFormat` |
| `About_` | About dialog | `About_Title`, `About_Version` |
| `Window_` | Window titles | `Window_Console`, `Window_Breakpoints` |
| `Breakpoints_` | Breakpoints window | `Breakpoints_ClearAll` |
| `Console_` | Console window | `Console_Log`, `Console_Live` |
| `Dialog_` | Common dialog buttons | `Dialog_OK`, `Dialog_Cancel` |
| `FileDialog_` | File open dialogs | `FileDialog_OpenElf` |
| `Msg_` | Message boxes | `Msg_Error`, `Msg_ElfLoaded` |
| `Settings_` | Settings window | `Settings_BoardType`, `Settings_Network` |

For `x:Uid` keys, append the property name: `BtnRun.Content`, `MenuFile.Title`, `DisasmTitle.Text`, etc.

Format strings use `{0}`, `{1}`, etc. as placeholders, replaced by `ResourceHelper::Format()`.
