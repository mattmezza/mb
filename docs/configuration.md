# Configuration core

`mb/config` is a standalone C++20 library that parses a strict TOML version 1
configuration document into plain standard-library data structures. Browser
startup integration and its GN target are pending. The separate file loader
and compiled control command can already validate files and inspect environment
paths. They never create environment directories, acquire browser locks, or
launch Chromium.

`LoadRuntimeConfig` combines that loader and environment audit into a value
snapshot. A caller supplies the filename, home directory, and optional explicit
and remembered environment names. An explicit name takes priority and an
unknown name fails with a `--environment` diagnostic. A remembered name is used
only when `app.restore_last_environment` is enabled; a stale name warns and
falls back to the configured default. All configured roots are audited before
a snapshot is returned, and no environment directories are created. Persistence
of the remembered name and consumption by browser startup remain pending.

The parser accepts at most 1 MiB of TOML, uses toml++ with exceptions disabled,
and returns all validation failures as diagnostics containing the supplied file
name, full configuration key, and source line when one is available. No
environment has parser defaults: at least one named environment is required.

```toml
schema_version = 1

[ui]
tabs_position = "left"
sidebar_width = 280
sidebar_collapsed = false
show_tab_close_buttons = true
theme = "system"

[app]
default_environment = "personal"
restore_last_environment = false

[environments.personal]
data_directory = "/absolute/path/chosen-by-the-user"
accent_color = "#336699"
startup_urls = ["https://example.invalid", "about:blank"]
```

Only `schema_version`, `ui`, `app`, and `environments` are permitted at the
root. `schema_version = 1` is required. Unknown keys are errors in every table,
which also rejects secret, command, or arbitrary extension fields.

`ui` is optional and defaults to `tabs_position = "left"`, `sidebar_width =
280`, `sidebar_collapsed = false`, `show_tab_close_buttons = true`, and `theme
= "system"`. `tabs_position` only permits `"left"`; `sidebar_width` must be an
integer from 126 through 400; and `theme` is `"system"`, `"light"`, or
`"dark"`.

`app` is optional. `default_environment` defaults to `"personal"` and must
name one configured environment. `restore_last_environment` defaults to false.

Each environment is a table under `environments`. Its name must match
`[A-Za-z0-9][A-Za-z0-9_-]{0,63}`. There may be at most 256 environments.
`data_directory` is a required, nonempty raw TOML string with no ASCII control
characters (C0/C1); it is retained without expansion or filesystem access by the
text parser.
`accent_color`, when present, is `#RRGGBB`. `startup_urls`, when present, is an
array of at most 100 strings.

Startup URLs reject control characters, whitespace, userinfo, and unsupported
schemes. The current conservative parser accepts lowercase `http://` and
`https://` URLs with a nonempty authority, `about:` URLs without an authority,
and `chrome://` URLs with a nonempty authority. Full Chromium `GURL` validation
and browser policy acceptance remain pending integration work.

Run the isolated tests from the repository root:

```sh
python3 mb/tools/test_config.py
```

The runner builds under `.build/config-tests`, uses the pinned Chromium
`clang++` when it exists, and otherwise emits a clear system-`g++` fallback
notice. It never writes under `.build/chromium/src`.

## Compiled control command

Build and exercise the companion command:

```sh
python3 mb/tools/build_control.py
.build/control/mbctl --config /path/to/config.toml config validate
.build/control/mbctl --config /path/to/config.toml environment list
.build/control/mbctl --config /path/to/config.toml environment path work
python3 -m unittest mb.test.test_control -v
```

The executable name is derived from the branding manifest's executable name
plus `ctl`. Without `--config`, it reads
`${XDG_CONFIG_HOME:-$HOME/.config}/<profile_directory_name>/config.toml`; with the
current manifest that is `~/.config/mb/config.toml`. Empty XDG_CONFIG_HOME uses
the fallback; a nonempty relative XDG_CONFIG_HOME is an error. HOME must be
absolute. Missing files are errors; this command does not generate user files.

Relative config filenames resolve from the invocation directory. Configuration
file symlinks are allowed: the loader canonicalizes the filename, and relative
environment roots resolve against that canonical file's parent directory.
The loader accepts regular files only, reads at most 1 MiB, and rejects FIFOs
without waiting on them. Environment paths then pass the nonmutating
[directory audit](environments.md), including collisions, symlinks, ownership,
permissions, and Linux path-length bounds. Missing environment roots are valid;
validation and listing do not create them. Errors exit with status 2 and include
the file/key; terminal control bytes in diagnostics are escaped.

The checked-in [example](../mb/config/example.toml) uses relative environment
paths to avoid baking the temporary product name into directory defaults.
Choose your desired absolute or `~/` roots when installing your configuration.

## Limits and change semantics

The vendored parser has a documented key-component guard and is configured for
at most 16 dotted-key components and 16 nested values. These limits are above
the depth required by the schema and reject excessive nesting before recursive
table allocation/destruction. Source provenance, the small local patch, and
checksums are under `mb/third_party/tomlplusplus`.

No live reload is implemented. The initial browser integration will load one
validated snapshot at process startup; configuration changes require all
windows/processes of the selected environment to exit before relaunch. A
second launch activating an existing Chromium process will not silently replace
that process's configuration. Schema version 1 is required; unsupported versions
fail without rewriting the file. There is no automatic migration yet.
