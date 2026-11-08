# KPairs

**A find-and-replace library for Notepad++.**

Keep named replacement pairs for different files and projects instead of retyping them into Find and Replace. Use Normal, Extended or Regex searches, organise pairs into groups, and preview changes before applying them.

Macros are useful for repeating a sequence. KPairs is useful when you have many individual replacements and want to find, edit and run just the one you need. For example, keep a **PyGimp** set for Python scripts, a **Klang** set for PHP files, and a cleanup set for text exports.

[Project](https://github.com/Krzysiu/nppKPairs) · [Changelog](CHANGELOG.md) · [Support](https://buymeacoffee.com/krzysiunet)

## Installation

You can compile KPairs yourself or install a binary from [Releases](https://github.com/Krzysiu/nppKPairs/releases). The binary package requires **64-bit Notepad++** and **Windows 10 or later**.

### Compile from source

Extract the source package. You need CMake 3.20+, a C++17 compiler and a Windows resource compiler.

With MinGW-w64 on PATH, run these commands in the source directory:

~~~powershell
cmake -S . -B build -G "MinGW Makefiles" -DCMAKE_BUILD_TYPE=Release
cmake --build build --parallel 4
ctest --test-dir build --output-on-failure
~~~

Use an x64 compiler for 64-bit or an i686 compiler for 32-bit, with a separate build directory for each. The DLL is in `build\KPairs.dll`.

With Visual Studio C++ Build Tools and the Windows SDK:

~~~powershell
cmake -S . -B build-vs -A x64
cmake --build build-vs --config Release
ctest --test-dir build-vs -C Release --output-on-failure
~~~

Use `-A Win32` and a different build directory for 32-bit. The DLL is in `build-vs\Release\KPairs.dll`.

### Install the plugin

Close Notepad++. Extract the **KPairs** folder from **KPairs-0.2-x64.zip** into your Notepad++ **plugins** directory.

The resulting layout must be:

~~~text
<Notepad++ installation>\plugins\KPairs\KPairs.dll
<Notepad++ installation>\plugins\KPairs\locale\
~~~

If your archive tool creates an extra folder named after the ZIP, move the **KPairs** folder from inside it into **plugins**.

Restart Notepad++ and open **Plugins → KPairs → Show KPairs**. Binary packages contain the plugin, translations and documentation; the src package contains the source and build files.

### Tested with
Tested with **Notepad++ 8.9.8.1 (64-bit)** on Windows. Automated tests cover replacement modes, selection boundaries, undo, preview, groups, search, favourites, import, statistics, translations and panel layout: 55 model assertions and 44 checks inside Notepad++. Wine has not been tested.

## Usage

A **pair** is one Find value, one Replace with value and their settings. A **group** organises pairs and supplies shared settings; running a pair does not run the rest of its group.

### Your first replacement

1. Open **Plugins → KPairs → Show KPairs**.
2. Click **Add** below the search field in the side panel.
3. Enter `Foo` in **Find**, `Bar` in **Replace with**, and choose **Normal** mode. Keep **Selection only** as the scope.
4. Add an optional name and click **Save**.
5. Select text containing `Foo` in your document, select the saved pair in the panel, and click **Preview** below the list.
6. Inspect the report, then click **Replace all** in the panel. **Ctrl+Z** undoes that execution.

A blank name displays **Find => Replace with**. An empty replacement deletes matches.

### Pair settings

| Setting | What it does |
| --- | --- |
| Name | Optional name shown in the library |
| Find / Replace with | The search and replacement values |
| Description | Notes or keywords used **only to find the pair in the library** |
| Group | Shared defaults for this pair |
| Colour | A marker to help recognise the pair |
| Favourite | Keeps the pair in Favourites |
| Search mode | Normal (literal text), Extended (escapes such as `\t` and `\n`), or Regex |
| Ignore case | Matches without distinguishing upper- and lowercase letters |
| Scope | Selection only or Whole document |
| Show only for | Filename and directory filters controlling when the pair appears |
| Use count | How often the pair has been executed; read-only |

The pair and group editors let you keep using Notepad++, for example to copy text into a pair.

### Side panel, favourites and context menu

Click the **heart beside a pair** to add or remove it from Favourites without executing it. Favourites are not repeated in Pairs by default; enable **Show favourites also in Pairs** if you prefer both.

Right-click a pair to edit, duplicate, delete, preview, execute or change its favourite status. The panel menu provides Settings, Groups, Statistics, Flag Guide and import/export. Sort buttons switch between name and creation date; clicking again reverses the order within each group.

In Settings, **Action on double click** can be **None**, **Execute replace**, **Edit pair** or **Preview**. Execute replace is the default. **Beep on complete** optionally sounds the system bell when execution finishes, including when no matches are found.

### Preview

**Preview shows a before-and-after report without changing your document.** It includes the match count and the first 100 replacement examples. This is useful for checking deletions and regex capture substitutions before running a pair.

The preview is a separate, copyable report, **not an on-canvas preview in the editor**. If you change the document or pair, generate a fresh preview. Preview does not count as an execution in usage statistics.

### Groups and filters

Open **Groups…** from the plugin menu or Settings. A group supplies defaults for colour, search mode, case sensitivity, scope and file/directory filters. Assign a pair to the group and leave **Use group setting** enabled for the settings it should inherit. Disable it to override a setting for that pair, including turning off an inherited filter.

For example, **PyGimp** can use filename `*.py`, directory `C:\Skrypty\Gimp` and **Recursive**. Its pairs then appear only for Python files in that directory and its subdirectories.

- **Wildcard mask (filename):** patterns such as `*.txt;*.php`. `*` and `?` are supported.
- **Directory:** one or more directory paths. **[+]** appends a selected folder without adding duplicates.
- **Recursive:** includes subdirectories.
- Separate entries with **semicolon (;)**. Entries within a field mean OR; active filename and directory filters must both match.
- Paths need not exist when you save them. A path that does not match simply keeps the pair hidden.
- **Show all** in Settings ignores these filters while retaining the library search.
- **Group display:** None, Name (a group heading), or Line (a separator).

Pairs without a group start with Regex, Ignore case, Selection only and no active filters. Deleting a group keeps its pairs and their effective settings.

Here are three independent sets you could keep:

| Group and target | Example pairs |
| --- | --- |
| **PyGimp:** `C:\Skrypty\Gimp\*.py`, recursive | Regex `[ \t]+$` → empty; Normal `# todo:` → `# TODO:` |
| **Klang:** `C:\Projects\Klang\*.php`, recursive | Normal `[legacy-log]` → `[klang]`; Normal `// todo:` → `// TODO:` |
| **Exports:** `C:\Exports\*.txt;*.tsv` | Extended `\t` → four spaces; Normal `<missing>` → `N/A` |

Enter directory and filename filters in their separate fields. Run only the replacement you need; preview it first.

### Library search and Description

**Description is exclusively a search aid.** Put notes or keywords there, such as **gimp python gimppy plugin**. Description does not affect the document replacement.

The search field at the top of the panel searches Name, Description, Find and Replace with, ignoring letter case:

- **AND:** every term must occur, in any order.
- **OR:** at least one term must occur.
- **FULL:** the complete query must occur as one continuous phrase, in that order.

Terms can match part of a word. For Description **gimp python gimppy plugin**, assuming no extra matches in the other fields:

| Keyword | Mode | Found |
| --- | --- | :---: |
| gimp plugin | AND | ✅ |
| gimp plugin python | AND | ✅ |
| imp lugin ython | AND | ✅ |
| gimp3 plugin python | AND | ❌ |
| gimp plugin python | OR | ✅ |
| gimp3 plugin python | OR | ✅ |
| gimp plugin python | FULL | ❌ |
| gimppy plugin | FULL | ✅ |
| gimppy plugin new | FULL | ❌ |

FULL matches a phrase within a field, not necessarily the entire field. **No results** appears in the list when a nonempty query finds nothing.

### Scope

Each pair uses **Selection only** or **Whole document**, directly or through its group. Whole document means the active document.

The panel's **Temporary scope** lets you use **Use pair settings**, **Selection only** or **Whole document** without changing the saved pair. It resets on restart. Selection only requires one nonempty ordinary selection; it never falls back to Whole document.

### Flags

Tags after the pair name show its effective settings:

| Tags | Meaning |
| --- | --- |
| N / E / R | Normal / Extended / Regex |
| CI / CS | Ignore case / Case-sensitive |
| F− / F | Filename filter off / on |
| D− / D / D+ | Directory filter off / exact directory / recursive directory |
| S / W | Selection only / Whole document |

In Settings, enable **Show flags on list** and choose which flags to show. Normally a flag appears only when it differs from the application default. **Display always** also shows its default state. For example, CI is the default: without Display always, only CS appears.

**Flag appearance…** changes each tag's text and colours. **Flag Guide** explains the current tags. The scope flag includes any Temporary scope override.

### Import and export

JSON import/export in the panel menu and Settings transfers pairs with their groups. Import appends entries without replacing the current library.

**Import (simple format)…** accepts a UTF-8 text file and lets you choose **Import as: Normal / Extended / Regex**. Regex is initially selected. Use one pair per line:

~~~text
A>Foo
B>Bar
C>Foo\>Bar
~~~

These create A → Foo, B → Bar and C → Foo>Bar. The first unescaped `>` separates the fields. `\>` means a literal >; `\\` means a literal backslash. Other escapes, such as `\d`, are preserved. **Spaces are preserved exactly.** Empty lines are ignored, and `A>` is a valid deletion pair. Invalid lines are reported before any entries are imported.

### Statistics and storage

**Gather usage statistics (local only; never sent)** is enabled by default. Open **Statistics…** from the plugin or panel menu to see executions, strings replaced and the top 10 pairs by each measure. Use **Copy report to clipboard** to copy it.

Two executions replacing 10 matches each give **2 executions and 20 strings replaced**. Preview and failed operations do not count; a completed execution with no matches counts once. Disabling statistics stops collection and retains existing counters.

The pair editor shows Use count, and Settings shows the number of saved pairs. Library data and settings are saved in `KPairs.json` in Notepad++'s plugin configuration directory. About shows the path. No usage data is sent over the network.

## Translations

**English and Polish are available.** KPairs automatically checks its locale files and selects a matching language, falling back to English. You can override the language in Settings.

To add a translation, translate the gettext `.pot` template into a `.po` file and compile it to `.mo` with Poedit or gettext. Place it at `locale/<language>/LC_MESSAGES/KPairs.mo` beside the plugin and restart Notepad++. Available language files are detected automatically.

**The name KPairs is never translated.** Your pair names, descriptions, patterns, replacements and paths are left as you entered them.

## Credits and licence

**Created and directed by Krzysztof Blachnicki.** The entire idea, feature requirements, settings and interface decisions are his. Code and documentation were generated with **ChatGPT — GPT-6 Astra**, under his direction.

KPairs is free software under **GPL-3.0-or-later**. See [LICENSE](LICENSE) and [third-party notices](THIRD_PARTY.md).

[GitHub](https://github.com/Krzysiu/nppKPairs) · [Buy Me a Coffee](https://buymeacoffee.com/krzysiunet)
