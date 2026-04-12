# テーマガイド (C++/WinUI3)

C++/WinUI3 版で採用しているダーク/ライト/システムテーマ切替の仕組みと、
ウィンドウやコントロールを追加する際に準拠すべき規約を解説します。

## アーキテクチャ概要

```
App.xaml
  ResourceDictionary.ThemeDictionaries
    x:Key="Dark"  -> ダークテーマのブラシキー定義
    x:Key="Light" -> ライトテーマのブラシキー定義

XAML:          {ThemeResource ThemeKeyName}
コード behind:  ResourceHelper::GetThemeBrush(L"ThemeKeyName")

App.xaml.cpp (コンストラクタ)
  -> Application.RequestedTheme を config に基づいて設定
  -> ResourceHelper::SetCurrentTheme(themeName)

MainWindow.xaml.cpp ApplyThemeToAllWindows(themeName)
  -> 全ウィンドウの Content に FrameworkElement.RequestedTheme を設定
  -> DwmSetWindowAttribute で各 HWND のタイトルバーを更新
  -> GetThemeBrush 経由でコード behind のブラシを再解決
  -> 動的コンテンツ (逆アセンブリ、ブレークポイント、コールスタック、メモリ) を更新
```

### C# WPF 版との主な違い

| 項目 | C# WPF | C++ WinUI3 |
|------|--------|------------|
| XAML マークアップ | `{DynamicResource}` | `{ThemeResource}` |
| テーマ辞書 | 別ファイル Dark.xaml/Light.xaml | App.xaml 内の ThemeDictionaries |
| 辞書切替 | `MergedDictionaries[0]` 差し替え | `FrameworkElement.RequestedTheme` |
| コード behind | `FindResource()` | `ResourceHelper::GetThemeBrush()` |
| アプリレベルテーマ | いつでも変更可 | `Application.RequestedTheme` はコンストラクタ後読取専用 |
| タイトルバー | DWM API (同じ) | DWM API (同じ) |

## ブラシキー一覧

| 分類 | キー名 | ダーク | ライト | 用途 |
|------|--------|--------|--------|------|
| 背景 | ThemeWindowBg | #1E1E1E | #F5F5F5 | ウィンドウ/ペイン背景 |
| 背景 | ThemePanelBg | #2D2D30 | #E8E8E8 | ツールバー、メニューバー |
| 背景 | ThemeControlBg | #3E3E42 | #D6D6D6 | ボタン背景 |
| 背景 | ThemeInputBg | #2D2D30 | #FFFFFF | TextBox 背景 (編集可能時) |
| 背景 | ThemeConsoleBg | #0C0C0C | #FFFFFF | コンソール端末 |
| 前景 | ThemeForeground | #D4D4D4 | #1E1E1E | 通常テキスト |
| 前景 | ThemeBrightFg | #F0F0F0 | #000000 | メニューテキスト |
| 前景 | ThemeDimFg | #808080 | #666666 | 補助テキスト |
| 前景 | ThemeDisabledFg | #909090 | #999999 | 無効化コントロール |
| 枠線 | ThemeBorder | #3F3F46 | #CCCCCC | コントロール枠線 |
| 枠線 | ThemeButtonBorder | #555555 | #AAAAAA | ボタン枠線 |
| アクセント | ThemeAccent | #569CD6 | #0066CC | セクションヘッダー |
| アクセント | ThemeAccentBanner | #0E639C | #0078D4 | 主要アクションボタン |
| ステータス | ThemeStatusBarBg | #007ACC | #007ACC | ステータスバー |
| ステータス | ThemeRunningFg | #90EE90 | #008000 | MHz 表示 |
| ステータス | ThemeWarningFg | #FFA500 | #CC6600 | 保留中/警告テキスト |
| ハイライト | ThemeHighlightFg | #FFFFFF | #FFFFFF | ステータスバー文字 |
| ハイライト | ThemeHighlightedFg | #FFFF00 | #996600 | 現在 PC、変更済み |
| ハイライト | ThemeCheckedBg | #264F78 | #B8D4F0 | 現在 PC 行の背景 |

全キーの一覧は `App.xaml` の ThemeDictionaries を参照してください。

## XAML ファイル追加時の規約

### 1. ウィンドウのルート背景

```xml
<Window x:Class="Em68030.MyWindow" ...>
    <Grid Background="{ThemeResource ThemeWindowBg}">
        ...
    </Grid>
</Window>
```

`Background="#FF1E1E1E"` のようなハードコード色は禁止です。
個別のウィンドウやダイアログに `RequestedTheme="Dark"` を設定しないでください。

### 2. XAML 内のコントロール

`{ThemeResource}` を使用します (`{StaticResource}` は不可):

```xml
<TextBlock Foreground="{ThemeResource ThemeForeground}" />
<TextBox Background="{ThemeResource ThemeInputBg}"
         Foreground="{ThemeResource ThemeForeground}"
         BorderBrush="{ThemeResource ThemeBorder}" />
<Button Background="{ThemeResource ThemeControlBg}"
        Foreground="{ThemeResource ThemeForeground}"
        BorderBrush="{ThemeResource ThemeButtonBorder}" />
```

### 3. コード behind で動的生成するコントロール

`ResourceHelper::GetThemeBrush()` を使用します。ThemeDictionaries から
現在のテーマに対応するブラシを正しく解決します:

```cpp
#include "Helpers/ResourceHelper.h"

auto fg = ::Em68030::ResourceHelper::GetThemeBrush(L"ThemeForeground");
textBlock.Foreground(fg);
```

**重要**: `Application::Current().Resources().Lookup()` はサブウィンドウで
ThemeDictionaries を正しく解決しません。必ず `ResourceHelper::GetThemeBrush()`
を使用してください。

### 4. WinUI3 TextBox のホバー/フォーカス状態の抑制

WinUI3 の TextBox はデフォルトでホバー時やフォーカス時に背景/前景を
変更します。読み取り専用の表示用 TextBox には `RegisterTextBoxStyle`
(MainWindow.xaml で定義済み) を使用してください。

カスタムテンプレートを使わずに状態変更を抑制する場合は、軽量スタイル
オーバーライドを使用:

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

表示前に `RequestedTheme` を設定:

```cpp
auto theme = ElementTheme::Dark;
if (config.Theme == "Light") theme = ElementTheme::Light;
else if (config.Theme == "System") theme = ElementTheme::Default;
dialog.RequestedTheme(theme);
co_await dialog.ShowAsync();
```

ContentDialog の XAML に `RequestedTheme="Dark"` を記述しないでください。

### 6. サブウィンドウ (Console, Breakpoints, CallStack)

新しいサブウィンドウ作成時に現在のテーマを適用:

```cpp
m_myWindow = winrt::make<implementation::MyWindow>();

// テーマ適用
auto theme = /* config から解決 */;
if (auto r = m_myWindow.Content().try_as<FrameworkElement>())
    r.RequestedTheme(theme);

// DWM タイトルバー設定
if (auto n = m_myWindow.try_as<::IWindowNative>()) {
    HWND hwnd = nullptr;
    n->get_WindowHandle(&hwnd);
    BOOL mode = isDark ? TRUE : FALSE;
    DwmSetWindowAttribute(hwnd, 20, &mode, sizeof(mode));
}
```

`ApplyThemeToAllWindows()` にもウィンドウを追加して、ランタイムの
テーマ変更が伝播するようにしてください。

### 7. コモンファイルダイアログ

WinUI3 のファイルダイアログは起動時の `Application.RequestedTheme` に
従います。ランタイムでのテーマ変更はファイルダイアログに反映されません
(`Application.RequestedTheme` はコンストラクタ後に読取専用のため)。

## 新しいブラシキーの追加方法

1. `App.xaml` の `x:Key="Dark"` と `x:Key="Light"` の**両方**の辞書にキーを追加
2. 両テーマで十分なコントラストが確保される色を選択
3. セマンティックな名前を使用 (例: `ThemeWarningFg`、`ThemeOrangeText` は不可)

## テーマ切替フロー

```
起動時:
  App::App() コンストラクタ
    -> config 読込、Application.RequestedTheme を設定
    -> ResourceHelper::SetCurrentTheme(config.Theme)
  MainWindow::MainWindow()
    -> ApplyThemeToAllWindows(config.Theme)

ランタイム (設定ダイアログ):
  MainWindow::ShowSettingsDialog() -> OK
    -> ApplyThemeToAllWindows(newTheme)
      -> ResourceHelper::SetCurrentTheme(effectiveTheme)
      -> 全ウィンドウの Content に RequestedTheme を設定
      -> DwmSetWindowAttribute で全 HWND のタイトルバー更新
      -> EnsureDisasmBrushes + 動的コンテンツ再描画
```

XAML の `{ThemeResource}` バインディングは `FrameworkElement.RequestedTheme`
の変更時に自動更新されます。コード behind のブラシはテーマ変更後に
`GetThemeBrush()` で再解決する必要があります。
