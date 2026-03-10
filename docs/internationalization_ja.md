# 国際化 (i18n)

Em68030 (C++/WinUI3) は Windows App SDK リソースファイル (`.resw`) を使用した多言語 UI に対応しています。
アプリケーション起動時に OS のロケール設定に基づいて自動的に言語が選択されます。

## 対応言語

| 言語 | カルチャ | リソースファイル |
|------|---------|----------------|
| 英語 (デフォルト) | `en-US` | `Strings/en-US/Resources.resw` |
| 日本語 | `ja-JP` | `Strings/ja-JP/Resources.resw` |

## アーキテクチャ

- **`Strings/{locale}/Resources.resw`** — 言語ごとのローカライズ済み文字列リソース。`.resw` 形式は `.resx` と同一の XML フォーマット。
- **`Helpers/ResourceHelper.h`** — `Microsoft::Windows::ApplicationModel::Resources::ResourceLoader` を使用するヘッダーオンリーのヘルパークラス。
- **XAML ローカライズ** — 要素に `x:Uid` 属性を使用。リソースキーの形式は `Uid名.プロパティ名` (例: `BtnRun.Content`, `MenuFile.Title`)。
- **コードビハインド ローカライズ** — 動的文字列は `ResourceHelper::GetString()` と `ResourceHelper::Format()` を使用。

### ResourceHelper API

```cpp
#include "Helpers/ResourceHelper.h"
using R = Em68030::ResourceHelper;

// 単純な文字列の取得
winrt::hstring text = R::GetString(L"Menu_File");

// std::wstring として取得
std::wstring text = R::GetStdString(L"Toolbar_Run");

// プレースホルダー {0}, {1}, ... を含むフォーマット文字列
std::wstring msg = R::Format(L"Status_FileFormat", filename);
std::wstring perf = R::Format(L"Status_MhzFormat", mhzStr, mipsStr);
```

## ローカライズしない文字列

ロケールに関係なく原文のまま表示される文字列:

- レジスタ名: D0–D7, A0–A7, PC, SR, SSP, VBR, FP0–FP7, CR, IAR
- フラグ名: X, N, Z, V, C, S, T
- ボード/OS 識別子: MVME147, Generic, NetBSD, Linux
- ステータスバーの技術用語: JIT, MIPS, MHz
- 診断/トレースメッセージ (`[EMU] ...`) — 開発者向け
- URL、バージョン番号

## 新しい文字列の追加手順

1. **`Strings/en-US/Resources.resw`** (英語) にキーと値を追加する。

2. **各ロケールファイル** (例: `Strings/ja-JP/Resources.resw`) に同じキーで翻訳済みの値を追加する。

3. **XAML 要素の場合**、`x:Uid` を使用する:
   ```xml
   <TextBlock x:Uid="MyNewLabel" />
   ```
   `.resw` ファイルにキー `MyNewLabel.Text` を追加する:
   ```xml
   <data name="MyNewLabel.Text" xml:space="preserve">
     <value>ラベルのテキスト</value>
   </data>
   ```
   `x:Uid` システムが `Uid名.プロパティ名` キーを要素のプロパティに自動的にマッピングする。

   一般的なプロパティサフィックス:
   | 要素 | プロパティキーサフィックス |
   |------|------------------------|
   | `TextBlock` | `.Text` |
   | `Button` | `.Content` |
   | `MenuBarItem` | `.Title` |
   | `MenuFlyoutItem` | `.Text` |
   | `ToggleButton` | `.Content` |
   | `CheckBox` | `.Content` |
   | `ContentDialog` | `.Title`, `.PrimaryButtonText`, `.CloseButtonText` |
   | `ToolTip` | `.Content` |

4. **コードビハインドの文字列の場合**、`ResourceHelper` を使用する:
   ```cpp
   #include "Helpers/ResourceHelper.h"
   // 単純なキー (x:Uid サフィックス不要)
   auto text = Em68030::ResourceHelper::GetString(L"Msg_Error");
   // フォーマット文字列
   auto msg = Em68030::ResourceHelper::Format(L"Status_FileFormat", filename);
   ```

5. **ビルドして検証** — MakePRI がビルド時に `.resw` ファイルを `resources.pri` にコンパイルする。

## 新しい言語の追加方法

1. **新しいディレクトリを作成する。** `Strings/{locale}/` (BCP 47 言語タグ):
   - `Strings/de-DE/Resources.resw` — ドイツ語
   - `Strings/zh-Hans/Resources.resw` — 簡体字中国語
   - `Strings/ko-KR/Resources.resw` — 韓国語

2. **`Strings/en-US/Resources.resw` を新しいディレクトリにコピーし、** すべての `<value>` 要素を翻訳する。すべてのキーが完全に一致する必要がある。

3. **`Em68030.vcxproj` に新しい `.resw` を追加する:**
   ```xml
   <ItemGroup>
     <PRIResource Include="Strings\en-US\Resources.resw" />
     <PRIResource Include="Strings\ja-JP\Resources.resw" />
     <PRIResource Include="Strings\de-DE\Resources.resw" />  <!-- 新規 -->
   </ItemGroup>
   ```

4. **ビルドする。** MakePRI が新しい言語を `resources.pri` に含める。

5. **検証する。** OS の表示言語を新しいロケールに設定する (または下記の強制ロケール方法を使用) してアプリケーションを起動する。

## ロケールの強制指定 (デバッグ / レイアウト確認)

### 方法 1: ResourceHelper の変更

`ResourceHelper::GetLoader()` を一時的に変更して言語を指定する:

```cpp
static winrt::Microsoft::Windows::ApplicationModel::Resources::ResourceLoader& GetLoader()
{
    static winrt::Microsoft::Windows::ApplicationModel::Resources::ResourceManager resMgr;
    // 英語を強制
    static auto context = resMgr.CreateResourceContext();
    context.QualifierValues().Insert(L"Language", L"en-US");
    // context.QualifierValues().Insert(L"Language", L"ja-JP");  // 日本語を強制

    static winrt::Microsoft::Windows::ApplicationModel::Resources::ResourceLoader loader;
    return loader;
}
```

または `ResourceManager` とカスタム `ResourceContext` を直接使用する:

```cpp
#include <winrt/Microsoft.Windows.ApplicationModel.Resources.h>

auto resMgr = winrt::Microsoft::Windows::ApplicationModel::Resources::ResourceManager();
auto context = resMgr.CreateResourceContext();
context.QualifierValues().Insert(L"Language", L"en-US");
auto candidate = resMgr.MainResourceMap().GetValue(L"Resources/Menu_File", context);
auto text = candidate.ValueAsString();
```

### 方法 2: Windows の優先言語

Windows 設定 > 時刻と言語 > 言語と地域 で優先言語の順序を変更する。目的の言語をリストの最上部に移動する。この変更はすべてのアプリケーションに影響する。

### 方法 3: プロセスごとの言語オーバーライド

`main()` または `wWinMain()` の先頭で `SetThreadPreferredUILanguages` Windows API を使用する:

```cpp
#include <windows.h>
#include <winnls.h>

// プロセス起動時に英語を強制
ULONG numLanguages = 0;
SetThreadPreferredUILanguages(MUI_LANGUAGE_NAME, L"en-US\0", &numLanguages);
```

**重要:** コミット前に強制ロケールのコードを削除またはコメントアウトしてください。デバッグ専用です。

## リソースキーの命名規則

| プレフィックス | 用途 | 例 |
|--------------|------|-----|
| `Menu_` | メニュー項目 | `Menu_File`, `Menu_OpenBinary` |
| `Toolbar_` | ツールバー ボタン | `Toolbar_Run`, `Toolbar_Stop` |
| `Disasm_` | 逆アセンブリ ペイン | `Disasm_Title`, `Disasm_Go` |
| `Regs_` | レジスタ ペイン | `Regs_Title`, `Regs_Edit` |
| `MemDump_` | メモリダンプ ペイン | `MemDump_Title`, `MemDump_Go` |
| `Context_` | コンテキスト メニュー | `Context_Copy`, `Context_Paste` |
| `Status_` | ステータスバー テキスト | `Status_Running`, `Status_MhzFormat` |
| `About_` | バージョン情報ダイアログ | `About_Title`, `About_Version` |
| `Window_` | ウィンドウ タイトル | `Window_Console`, `Window_Breakpoints` |
| `Breakpoints_` | ブレークポイント ウィンドウ | `Breakpoints_ClearAll` |
| `Console_` | コンソール ウィンドウ | `Console_Log`, `Console_Live` |
| `Dialog_` | 共通ダイアログ ボタン | `Dialog_OK`, `Dialog_Cancel` |
| `FileDialog_` | ファイルを開くダイアログ | `FileDialog_OpenElf` |
| `Msg_` | メッセージ ボックス | `Msg_Error`, `Msg_ElfLoaded` |
| `Settings_` | 設定ウィンドウ | `Settings_BoardType`, `Settings_Network` |

`x:Uid` キーの場合、プロパティ名を追加する: `BtnRun.Content`, `MenuFile.Title`, `DisasmTitle.Text` など。

フォーマット文字列はプレースホルダーとして `{0}`, `{1}` 等を使用し、`ResourceHelper::Format()` で置換する。
