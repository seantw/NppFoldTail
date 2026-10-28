# NppFoldTail

A Notepad++ plugin for cleaner, more readable code folding.

```js
setTimeout(() => { … }, 1000);
function example() { … } // first comment ～ // last comment
```

```html
<section> … </section>
<!-- … -->
```

```python
def example(): …
```

## Features

- Shows closing delimiters and hides trailing header text without modifying the document.
- Click `…` to expand; hover to preview the first 10 source lines. Longer blocks end with a separate `⋮` line, approximately centred below the last displayed nonblank line.
- The ` … ` marker uses the foreground and background colours of Notepad++'s **Brace highlight style**. Selection colours take precedence while the folded block is selected.
- Optionally shows trailing comments from the first and last folded lines, using the language's single-line comment colour.
- Remembers menu settings across Notepad++ restarts.
- Highlights the preview when the hidden block is selected. Selecting through a collapsed header extends the actual selection through the hidden content for copying or deletion.
- Reuses cached preview windows while scrolling and repaints only when their content or appearance changes, reducing flicker with many folded blocks.
- Hides folding lines only for supported languages; other languages retain Notepad++'s normal folding display.

## Supported languages

| Languages | Preview |
|---|---|
| C, C++, C#, Java, JavaScript, TypeScript, Go, Swift | Closing delimiters and supported block comments |
| CSS, Perl, PHP, Rust, PowerShell, R | Brace blocks; comment support varies as noted below |
| JSON, JSON5 | Objects and arrays |
| HTML, XML | Elements and comments, plus brace blocks and comments in embedded JavaScript; XML tag names are case-sensitive |
| HTML `<style>` (also in PHP documents) | Adds folding for multiline CSS brace blocks and `/* */` comments, including nested rules |
| Python, YAML (.yml/.yaml) | Ellipsis only, without a closing delimiter |

## Install

Requires Windows and Notepad++ x64. x86 and ARM64 builds are not provided.

Manually tested with Notepad++ v8.9.8.1 (64-bit). Compatibility with other versions has not been verified.

1. Download `NppFoldTail-0.9.0-x64.zip` from [Releases](https://github.com/seantw/NppFoldTail/releases).
2. Close all Notepad++ windows.
3. Extract the ZIP and place `NppFoldTail.dll` in `Notepad++/plugins/NppFoldTail/`.
4. Restart Notepad++ and check **Plugins → NppFoldTail → About NppFoldTail** for version **0.9.0**.

## Options

Under **Plugins → NppFoldTail**:

| Option | Default | Behaviour |
|---|---|---|
| Enable | On | Turns folding previews on or off |
| First Comment | Off | Appends a trailing comment from the folded header |
| Last Comment | Off | Appends a trailing comment from the closing line |

When both selected comments are present, they are separated by ` ～ `. No separator is added if either selected comment is missing. Comment blocks themselves are not duplicated as trailing comments. These options affect only the preview; source text and tooltip contents are unchanged.

All three settings are saved immediately to `NppFoldTail.ini` in Notepad++'s plugin configuration directory and restored on restart.

## Selecting folded content

For normal single-caret navigation, Right at a collapsed header's display boundary and End on that header jump to the next visible line's start. Left at that next line's start returns to the boundary. Up/Down navigation clamps the caret to the visible header text. Clicking synthetic text or blank space after `…` moves the caret to the next visible line's start; clicking `…` still expands the block. If there is no next visible line, the caret stays at the folded header's display boundary. Ctrl/Alt shortcuts and rectangular/multiple selections retain their normal handling.

When a single normal text selection reaches the end of a collapsed header, it extends through the complete hidden block and its closing line, excluding the following newline. The preview receives the selection background colour.

Copy and delete operate on the selected original source, including hidden content, never on the synthetic preview. A caret without a selection does not trigger this behaviour. Rectangular and multiple selections are left unchanged.

## Build

Requires x86_64 MinGW-w64 `g++` / `windres` on PATH. The development environment uses Strawberry Perl's MinGW-w64 tools.

```powershell
.\build-x64.bat
```

Creates `build/NppFoldTail.dll` using size optimisation and static C++ runtime linking.

GitHub Actions also compiles x64 and x86 builds. For releases, use the tested x64 CI ZIP, which contains `NppFoldTail.dll` and `LICENSE` at its root. The x86 CI build is experimental and has not been manually tested or published for Plugins Admin.

## Limitations

- Wrapped header lines and multiline HTML/XML opening tags are not supported.
- PHP support covers brace blocks and `/* */` comments, not alternative `endif`/`endforeach` syntax.
- Embedded script languages other than JavaScript and PHP, and XML CDATA previews, are not supported.
- PowerShell block comments are not previewed.
- Synthetic preview text cannot be edited directly.
- Embedded CSS folds are rebuilt after text or lexer changes, not while scrolling. Strings and URLs are excluded from brace/comment matching. Only complete multiline pairs create new folds; disabling the plugin restores native folding. CSS colours remain those provided by Notepad++'s HTML mode.

## Changes in 0.9.0

- Adds folding for multiline CSS brace blocks and comments inside HTML `<style>` elements, including nested rules and `@media` blocks.
- Adds folding previews for JavaScript blocks and comments embedded in HTML `<script>` elements.
- Moves the caret to the next visible line's start when clicking synthetic text or blank space after `…`; clicking `…` still expands the block.
- Adds a clickable GitHub project link to the About dialog.
- Preserves existing preview, tooltip and whole-block selection behaviour for the new embedded-language support.

## Changes in 0.8.3

- Switches release packaging to the GitHub Actions x64 build.
- Fixes CI tool paths and MinGW-w64 CRT linking, and removes the unused make dependency.
- Packages the versioned release ZIP with the DLL and LICENSE at its root.
- Folding behaviour is unchanged from 0.8.2.

## Changes in 0.8.2

- Reduces scrolling flicker by reusing preview bitmaps and windows, batching position updates and limiting work to visible previews.
- Uses the active theme's Brace highlight style foreground and background colours for ` … `, while keeping selection highlighting.
- Makes spacing around `…` consistent for Python and YAML.
- Adds intuitive caret navigation around folded content: Right/End move to the next visible line; Left can return to the folded header boundary.
- Prevents caret movement and clicks from placing the caret inside obscured header text. Shift selection continues to select the actual hidden block.

## Changes in 0.8.1

- Updates the plugin description in the README, About dialog and DLL file properties to better describe its purpose. Folding behaviour is unchanged from 0.8.0.

## Changes in 0.8.0

- Adds Python, YAML, JSON/JSON5, PHP, XML, Rust, PowerShell and R support.
- Unifies tooltips to the first 10 source lines.
- Adds optional first/last comment display and persistent menu settings.
- Adds whole-block selection and matching preview highlighting.
- Fixes preview flicker and hand/text cursor switching over `…`.
- Preserves native folding lines for unsupported languages.

## License

[MIT](LICENSE). Release ZIPs include the DLL and LICENSE.
