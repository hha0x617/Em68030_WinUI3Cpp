# Contributing to Em68030_WinUI3Cpp

Thanks for your interest!  This is the **C++/WinRT + WinUI 3** port of the
MC68030 emulator targeting the MVME147 single-board computer — the
high-performance sibling of [Em68030_CsWPF](https://github.com/hha0x617/Em68030_CsWPF).

## Getting the source

```bash
git clone https://github.com/hha0x617/Em68030_WinUI3Cpp.git
```

## Build prerequisites

- Visual Studio 2022+ with the **Desktop development with C++** workload
- **Windows App SDK** (installed via VS installer or NuGet restore)
- Windows 10 1809 or later

## Building

```bash
msbuild Em68030_WinUI3Cpp.sln -restore -p:Configuration=Release -p:Platform=x64
```

Note: the vcxproj targets `PlatformToolset=v145` (Visual Studio 18).
On older runners/VS versions, override with
`-p:PlatformToolset=v143`.  Output lands under `Em68030/x64/Release/`.

Run the tests:

```bash
Em68030/x64/Release/Em68030.Tests.exe
```

## Making a change

1. Fork the repository and create a feature branch off `main`.
2. Keep commits focused; write commit messages that explain the *why*.
3. Add or update Google Test cases (under `Em68030.Tests/`) for behaviour
   changes.
4. Open a pull request against `main`.  CI must pass before merge.

## Commit style

- Subject line ≤ 72 chars, imperative mood, optional `type(scope):` prefix
  (`feat(cpu):`, `fix(mmu):`, `docs:`, `ci:`, `chore:`).
- Body wrapped to 72 chars, focused on motivation and trade-offs.
- For CPU correctness fixes, include the failing case (instruction,
  operands, expected vs observed register / memory state) so reviewers
  can reproduce it quickly.

## WinUI 3 theming caveats

WinUI 3's `Microsoft.UI.Xaml.Window` is **not** a `FrameworkElement`,
so it cannot hold `RequestedTheme` itself. The theme lives on
`Window.Content`'s root, and is **dropped on every Content swap** —
the next render falls back to the system default (Light) until something
re-applies the theme.

This codebase mostly works around it by keeping content static (the
Settings dialog is a XAML-defined `ContentDialog` rather than a
dynamic Grid), so `MainWindow::ApplyTheme` calling `RequestedTheme`
once per Window covers the common cases. **However**, if you ever
introduce a window whose `Content` is rebuilt from code-behind
(e.g. swapping `Grid` instances on a combo change), make sure to
re-call `ApplyTheme` (or its `RequestedTheme` setter) after each
swap, or the dialog will silently drop dark mode on the rebuild.

(The plugin frontend `emfe_WinUI3Cpp` ran into this exact issue with
its dynamic Settings dialog and now routes all Content swaps through
a `SetThemedWindowContent` helper — see that repo's CONTRIBUTING for
the rule.)

## Reporting bugs / requesting features

Use the issue templates in [`.github/ISSUE_TEMPLATE/`](.github/ISSUE_TEMPLATE/).
Security vulnerabilities go through [`SECURITY.md`](SECURITY.md) instead.

## Code of Conduct

This project follows the [Contributor Covenant 2.1](CODE_OF_CONDUCT.md).
By participating you are expected to uphold those standards.  Reports
of unacceptable behaviour go to the contact address listed in the
Code of Conduct.

## Parallel C# port

The sibling C#/WPF port lives at
[Em68030_CsWPF](https://github.com/hha0x617/Em68030_CsWPF).  The two share
the same MC68030 ISA semantics and MVME147 device set — fixes to the
CPU/devices in one port usually want a mirror PR in the other.

## License

By submitting a contribution you agree it will be licensed under the
**Apache-2.0** terms as the rest of the repository.
