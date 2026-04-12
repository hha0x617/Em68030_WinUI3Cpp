# Theming Guide (C++/WinUI3)

This document explains the Dark/Light/System theme switching mechanism
used in the C++/WinUI3 version and the rules developers must follow
when adding new windows, controls, or dialogs.

## Architecture Overview

```
App.xaml
  ResourceDictionary.ThemeDictionaries
    x:Key="Dark"  -> all brush keys for dark theme
    x:Key="Light" -> all brush keys for light theme

XAML files use:        {ThemeResource ThemeKeyName}
Code-behind uses:      ResourceHelper::GetThemeBrush(L"ThemeKeyName")

App.xaml.cpp (constructor)
  -> Application.RequestedTheme = Dark/Light based on config
  -> ResourceHelper::SetCurrentTheme(themeName)

MainWindow.xaml.cpp ApplyThemeToAllWindows(themeName)
  -> FrameworkElement.RequestedTheme on each window's Content
  -> DwmSetWindowAttribute for title bar chrome on each HWND
  -> Re-resolves code-behind brushes via GetThemeBrush
  -> Refreshes dynamic content (disasm, breakpoints, callstack, memory)
```

### Key Differences from C# WPF

| Aspect | C# WPF | C++ WinUI3 |
|--------|--------|------------|
| XAML markup | `{DynamicResource}` | `{ThemeResource}` |
| Theme dictionary | Separate Dark.xaml/Light.xaml files | ThemeDictionaries in App.xaml |
| Dictionary swap | `MergedDictionaries[0]` replacement | `FrameworkElement.RequestedTheme` |
| Code-behind | `FindResource()` | `ResourceHelper::GetThemeBrush()` |
| App-level theme | Can be changed anytime | `Application.RequestedTheme` is read-only after constructor |
| Title bar | DWM API per-window | DWM API per-window (same) |

## Brush Key Reference

| Category | Key | Dark | Light | Usage |
|----------|-----|------|-------|-------|
| Background | ThemeWindowBg | #1E1E1E | #F5F5F5 | Window/pane background |
| Background | ThemePanelBg | #2D2D30 | #E8E8E8 | Toolbar, menu bar |
| Background | ThemeControlBg | #3E3E42 | #D6D6D6 | Button background |
| Background | ThemeInputBg | #2D2D30 | #FFFFFF | TextBox background (editable) |
| Background | ThemeConsoleBg | #0C0C0C | #FFFFFF | Console terminal |
| Foreground | ThemeForeground | #D4D4D4 | #1E1E1E | Primary text |
| Foreground | ThemeBrightFg | #F0F0F0 | #000000 | Menu text |
| Foreground | ThemeDimFg | #808080 | #666666 | Subtle/secondary text |
| Foreground | ThemeDisabledFg | #909090 | #999999 | Disabled controls |
| Border | ThemeBorder | #3F3F46 | #CCCCCC | Control borders |
| Border | ThemeButtonBorder | #555555 | #AAAAAA | Button borders |
| Accent | ThemeAccent | #569CD6 | #0066CC | Section headers, links |
| Accent | ThemeAccentBanner | #0E639C | #0078D4 | Primary action button |
| Status | ThemeStatusBarBg | #007ACC | #007ACC | Status bar |
| Status | ThemeRunningFg | #90EE90 | #008000 | MHz display |
| Status | ThemeWarningFg | #FFA500 | #CC6600 | Pending/warning text |
| Highlight | ThemeHighlightFg | #FFFFFF | #FFFFFF | Status bar text |
| Highlight | ThemeHighlightedFg | #FFFF00 | #996600 | Current PC, modified |
| Highlight | ThemeCheckedBg | #264F78 | #B8D4F0 | Current PC background |

See `App.xaml` ThemeDictionaries for the complete list.

## Rules for New XAML Files

### 1. Window Root Background

```xml
<Window x:Class="Em68030.MyWindow" ...>
    <Grid Background="{ThemeResource ThemeWindowBg}">
        ...
    </Grid>
</Window>
```

Never use hardcoded hex colors like `Background="#FF1E1E1E"`.
Never set `RequestedTheme="Dark"` on individual windows or dialogs.

### 2. Controls in XAML

Use `{ThemeResource KeyName}` (not `{StaticResource}`):

```xml
<TextBlock Foreground="{ThemeResource ThemeForeground}" />
<TextBox Background="{ThemeResource ThemeInputBg}"
         Foreground="{ThemeResource ThemeForeground}"
         BorderBrush="{ThemeResource ThemeBorder}" />
<Button Background="{ThemeResource ThemeControlBg}"
        Foreground="{ThemeResource ThemeForeground}"
        BorderBrush="{ThemeResource ThemeButtonBorder}" />
```

### 3. Controls Created in Code-Behind

Use `ResourceHelper::GetThemeBrush()` which resolves from the correct
ThemeDictionary based on the current theme:

```cpp
#include "Helpers/ResourceHelper.h"

auto fg = ::Em68030::ResourceHelper::GetThemeBrush(L"ThemeForeground");
textBlock.Foreground(fg);
```

**Important**: `Application::Current().Resources().Lookup()` does NOT
reliably resolve ThemeDictionaries for sub-windows. Always use
`ResourceHelper::GetThemeBrush()` instead.

### 4. WinUI3 TextBox Hover/Focus Overrides

WinUI3 TextBox changes background/foreground on hover and focus by
default. For read-only display TextBoxes, use `RegisterTextBoxStyle`
(defined in MainWindow.xaml) which provides a custom template without
these state changes.

For TextBoxes that need to suppress state changes without using the
full custom template, use lightweight styling:

```xml
<TextBox ...>
    <TextBox.Resources>
        <StaticResource x:Key="TextControlForegroundFocused" ResourceKey="ThemeForeground" />
        <StaticResource x:Key="TextControlBackgroundFocused" ResourceKey="ThemeWindowBg" />
        <StaticResource x:Key="TextControlForegroundPointerOver" ResourceKey="ThemeForeground" />
        <StaticResource x:Key="TextControlBackgroundPointerOver" ResourceKey="ThemeWindowBg" />
    </TextBox.Resources>
</TextBox>
```

### 5. ContentDialog (Settings, About, Input)

Set `RequestedTheme` before showing:

```cpp
auto theme = ElementTheme::Dark;
if (config.Theme == "Light") theme = ElementTheme::Light;
else if (config.Theme == "System") theme = ElementTheme::Default;
dialog.RequestedTheme(theme);
co_await dialog.ShowAsync();
```

Do NOT set `RequestedTheme="Dark"` in the XAML of ContentDialog.

### 6. Sub-Windows (Console, Breakpoints, CallStack)

When creating a new sub-window, apply the current theme to its content:

```cpp
m_myWindow = winrt::make<implementation::MyWindow>();

// Apply theme
auto theme = /* resolve from config */;
if (auto r = m_myWindow.Content().try_as<FrameworkElement>())
    r.RequestedTheme(theme);

// Apply DWM title bar
if (auto n = m_myWindow.try_as<::IWindowNative>()) {
    HWND hwnd = nullptr;
    n->get_WindowHandle(&hwnd);
    BOOL mode = isDark ? TRUE : FALSE;
    DwmSetWindowAttribute(hwnd, 20, &mode, sizeof(mode));
}
```

Also add the window to `ApplyThemeToAllWindows()` so runtime theme
changes propagate to it.

### 7. Common File Dialogs

WinUI3 file dialogs follow the `Application.RequestedTheme` set at
startup. Runtime theme changes do not affect file dialogs because
`Application.RequestedTheme` is read-only after construction.

## Adding a New Brush Key

1. Add the key to **both** the `x:Key="Dark"` and `x:Key="Light"`
   dictionaries in `App.xaml`
2. Choose colors that provide adequate contrast in both themes
3. Use a semantic name (e.g., `ThemeWarningFg`, not `ThemeOrangeText`)
4. Update `ResourceHelper::SetCurrentTheme` is not needed -- the
   helper reads from ThemeDictionaries automatically

## Theme Switching Flow

```
Startup:
  App::App() constructor
    -> Load config, set Application.RequestedTheme
    -> ResourceHelper::SetCurrentTheme(config.Theme)
  MainWindow::MainWindow()
    -> ApplyThemeToAllWindows(config.Theme)

Runtime (Settings dialog):
  MainWindow::ShowSettingsDialog() -> OK
    -> ApplyThemeToAllWindows(newTheme)
      -> ResourceHelper::SetCurrentTheme(effectiveTheme)
      -> FrameworkElement.RequestedTheme on all window Contents
      -> DwmSetWindowAttribute on all window HWNDs
      -> EnsureDisasmBrushes + refresh dynamic content
```

XAML `{ThemeResource}` bindings update automatically when
`FrameworkElement.RequestedTheme` changes. Code-behind brushes
must be re-resolved via `GetThemeBrush()` after theme change.
