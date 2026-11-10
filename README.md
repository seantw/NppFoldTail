
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
- Click the folding marker to expand; hover to preview the first 10 source lines. The preview removes the first nonblank line's indentation from every line, preserving relative indentation. Longer blocks end with a separate `⋮` line, approximately centred below the last displayed nonblank line.
- The ` … ` marker uses the foreground and background colours of Notepad++'s **Brace highlight style**. Selection colours take precedence while the folded block is selected.
- Optionally shows trailing comments from the first and last folded lines, using the language's single-line comment colour.
- Provides a Settings dialog for comments, embedded CSS folding, Markdown heading folding and optional line spacing, while keeping Enable as a quick menu toggle.
- Remembers settings across Notepad++ restarts and preserves existing preferences when upgrading.
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
| Markdown (.md/.markdown, opt-in) | Heading sections with a vertical ellipsis (`⋮`) |

## Install

Requires Windows and Notepad++ x64. x86 and ARM64 builds are not provided.

Manually tested with Notepad++ v8.9.8.1 (64-bit). Compatibility with other versions has not been verified.

1. Download `NppFoldTail-0.12.0-x64.zip` from [Releases](https://github.com/seantw/NppFoldTail/releases).
2. Close all Notepad++ windows.
3. Extract the ZIP and place `NppFoldTail.dll` in `Notepad++/plugins/NppFoldTail/`.
4. Restart Notepad++ and check **Plugins → NppFoldTail → About NppFoldTail** for version **0.12.0**.

## Options

Under **Plugins → NppFoldTail**, **Enable** remains a quick toggle. Open **Settings...** to configure all options:

| Option | Default | Behaviour |
|---|---|---|
| Enable | On | Turns folding previews on or off |
| First Comment | Off | Appends a trailing comment from the folded header |
| Last Comment | Off | Appends a trailing comment from the closing line |
| Fold CSS inside `<style>` | On | Adds folds for embedded CSS brace blocks and multiline comments |
| Fold Markdown headings | Off | Adds heading folds in Markdown files without changing UDL syntax highlighting |
| Fold Markdown blocks | Off | Folds tables, fenced code blocks, outermost lists and consecutive link reference definitions |
| Custom line spacing | Off | Sets extra space above and below each display line in both editor views |
| Space above / below | 0 / 0 pixels | Each side accepts a whole number from 0 to 20 when custom spacing is enabled |

When both selected comments are present, they are separated by ` ～ `. No separator is added if either selected comment is missing. Comment blocks themselves are not duplicated as trailing comments. These options affect only the preview; source text and tooltip contents are unchanged.

**Apply** saves the settings and updates the editor immediately. **OK** applies and closes the dialog. **Cancel** discards changes made since the last Apply. **Restore Defaults** fills in the default values; use Apply or OK to save them.

Settings are saved to `NppFoldTail.ini` in Notepad++'s plugin configuration directory and restored on restart. Existing Enable / First Comment / Last Comment values are preserved when upgrading.

Custom line spacing is off by default, so the plugin leaves existing editor spacing alone. Turning custom spacing or Enable off restores each view's original spacing. Spacing affects display only, across languages, and does not change font size or document text. CSS folding can be disabled independently while other previews remain enabled.

## Markdown folding

Enable **Fold Markdown headings** in **Settings...**, then open a `.md` or `.markdown` file. ATX headings (`#` through `######`) fold everything below them up to, but excluding, the next heading of the same or higher level. If there is no such heading, the fold extends to the end of the document. Headings in fenced code blocks are ignored. Setext headings (underlined with `=` or `-`) are not supported.

A collapsed heading displays `⋮`. Hover over it to preview the section, click it to expand, or click the space to its right to move to the next visible line. Turning off the option removes the plugin's heading folds. Use a Markdown UDL or plain text mode; a file assigned to another lexer already supported by NppFoldTail keeps that lexer's folding behaviour.

Enable **Fold Markdown blocks** to fold tables, fenced code blocks, outermost ordered or unordered lists and consecutive link reference definitions (`[label]: URL`) independently of headings. A folded block shows `⋮` after its first line. Code fences work with or without a language name. Nested list content folds with the outer list, including unindented continuation lines after a two-space hard break. Fenced code does not create internal heading or block folds. When both options are enabled, blocks can fold individually or together under their parent heading.

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

GitHub Actions also compiles x64 and x86 builds. Releases may use either a local x64 build or the x64 CI artifact. Prefer a build manually tested in Notepad++. Future release ZIPs contain only `NppFoldTail.dll` at the root. Before publishing, check that the DLL resource ProductVersion and FileVersion correspond to the version submitted to nppPluginList (for example, `0.10.0` / `0.10.0.0`), and calculate the SHA-256 from the final uploaded ZIP. The x86 CI build is experimental and has not been manually tested or published for Plugins Admin.

## Limitations

- Wrapped header lines and multiline HTML/XML opening tags are not supported.
- PHP support covers brace blocks and `/* */` comments, not alternative `endif`/`endforeach` syntax.
- Embedded script languages other than JavaScript and PHP, and XML CDATA previews, are not supported.
- PowerShell block comments are not previewed.
- Synthetic preview text cannot be edited directly.
- Embedded CSS folds are rebuilt after text or lexer changes, not while scrolling. Strings and URLs are excluded from brace/comment matching. Only complete multiline pairs create new folds; disabling the plugin restores native folding. CSS colours remain those provided by Notepad++'s HTML mode.

## Changes in 0.12.0

- Adds a separate Markdown block folding option for tables, fenced code, outermost lists and consecutive link reference definitions.
- Supports code fences without a language name and list continuation lines after two-space hard breaks.
- Lets Markdown blocks fold independently or together with their parent heading.

## Changes in 0.11.0

- Adds optional Markdown heading folding for `.md` and `.markdown` files, with a vertical three-dot marker.
- Draws fold markers independently of the editor font. Markers scale and align to the visible text above 12 pt.
- Removes shared leading indentation from tooltip previews while preserving relative indentation.
- Packages the x64 DLL by itself at the ZIP root.

## Changes in 0.10.0

- Moves First Comment and Last Comment into a Settings dialog, with Enable retained as a quick menu toggle.
- Adds an independent switch for CSS folding inside HTML `<style>` elements.
- Adds optional extra line spacing above and below text (0-20 pixels per side) in both editor views, with folded preview text aligned to the adjusted spacing.
- Adds Apply, OK, Cancel and Restore Defaults controls. Settings are saved to the existing INI file and restored on restart.
- Preserves existing preferences. Custom line spacing is off by default; disabling it or the plugin restores each view's original spacing.

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

[MIT](LICENSE). The license is available in this repository.
