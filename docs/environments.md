# Environment directory core

Status (2026-09-08): a standalone Linux C++20 library and GoogleTests exist in
the product repository. They are not integrated into the Chromium source tree
or browser startup. Runtime environment isolation, singleton activation and
locking, browser configuration integration, and private-browsing behavior remain
untested. The standalone control command integrates the parser and nonmutating
audit; its command-level tests pass. The upstream browser build/launch gate still
applies.

## Resolution API

`mb/browser/environment_paths.h` exposes three operations independent of the
TOML parser. Callers supply the absolute configuration-file path, absolute home
directory, and a map from environment name to raw `data_directory` string.
The library does not read process environment variables or infer home paths.

`ResolveEnvironmentPaths` performs pure validation and lexical resolution. It
does not read or write the filesystem. A relative directory resolves against
the configuration file's parent; `~/` resolves against the supplied home;
absolute paths stay absolute. Repeated slashes and `.` components normalize.
All `..` components are rejected, including those that would lexically remain
inside a parent. This avoids differences between lexical normalization and
filesystem traversal. `~other` and bare `~` are rejected. Shell substitutions
and `$VARIABLE` text are literal filename text, never executed or expanded.

Names and paths must be nonempty, valid UTF-8 without C0/C1 controls, including
NUL. Each component is limited to `NAME_MAX` bytes (255 on Linux), and each
normalized absolute path must be shorter than `PATH_MAX` bytes (4096 on Linux,
including the terminating NUL). These byte limits apply during pure resolution
to roots after expansion and to the configuration-file and home paths. They
prevent creating deep roots that Chromium cannot subsequently reopen by their
absolute paths. Actual filesystem-specific limits may be lower and still cause
preparation errors. An environment cannot use `/`. Duplicate normalized roots and nested
roots are rejected across every configured environment, including unselected
entries. `/data/work` and `/data/work2` remain distinct sibling roots.

All APIs return `bool` and fill an explicit `EnvironmentPathError` with a
code, key such as `environments.work.data_directory`, explanation, and errno
where applicable. Output pointers must be non-null and must not alias input
objects. Resolution clears its result on failure; preparation releases any
previous result before attempting a new preparation.

## Filesystem preparation

`AuditEnvironmentPaths` adds a nonmutating filesystem audit to pure resolution.
Missing roots and parent directories are allowed; existing components must
satisfy the security policy below. It returns resolved paths on success and
clears the result on failure. Configuration validation and listing can use it
without creating profile directories.

`PrepareEnvironmentRoot` calls this audit, verifies the selection, and creates
the selected root only after all configured existing paths pass inspection.
Inspection and creation walk from `/` using descriptor-relative `openat` with
`O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC`, followed by `fstat` on the opened
descriptor. Symlinks are forbidden at every component, including dangling
symlinks and unselected roots. Thus the returned normalized absolute path is
also the canonical spelling under this policy. Paths through symlinked home
directories or symlinked system aliases must be replaced by their real paths.
Ordinary root-owned `/home` and user-owned home directories are supported.

Existing ancestors must be owned by root or the effective user and cannot be
group/other writable. Root-owned sticky directories such as a conventional
`/tmp` are allowed as ancestors: the sticky rule prevents another user from
replacing entries owned by the current user or root. A final existing root
must belong to the effective user, have owner read/write/execute permission,
and grant no group/other access. Unsafe existing permissions produce an error;
the library never repairs them by changing permissions.

Existing device/inode identities and the remaining path components are also
compared across the inspected paths to detect aliases and overlaps exposed
by bind mounts, even when suffix directories do not exist. No mounts or mount
namespace changes are performed by the library or test suite. Bind-mount
alias behavior has not been exercised with real mounts in tests.

Only missing components of the selected root are created, with `mkdirat(...,
0700)`. The library never changes process umask, chmods any path, or deletes
anything. Normal umasks such as 0022 and 0077 preserve mode 0700; an unusual
umask removing owner permissions can cause preparation to fail. A creation
failure may leave private parent directories behind. No creation occurs for
an invalid specification or an unsafe existing path found during inspection.

Concurrent preparations of the same root tolerate `mkdirat` returning
`EEXIST` and validate the directory actually opened afterward. There is no
separate `exists` check. A successful `PreparedEnvironmentRoot` owns a directory
descriptor and its absolute path; it is movable and closes its descriptor on
destruction. The descriptor itself is not a process lock.

The policy protects against filesystem replacement by other unprivileged
users. A process with the same UID, root privileges, or authority to change
the mount namespace can change the filesystem after inspection. Holding a
descriptor does not make later Chromium path lookups refer to that descriptor.
Cross-environment validation is not an atomic filesystem snapshot. A later
browser integration must respect this boundary and retain Chromium's own
process singleton and IPC activation; no custom singleton lock is implemented.

## Observed validation

Run `python3 mb/tools/test_environment_paths.py`. It builds outside Chromium at
`.build/environment-tests/environment_paths_test`, uses temporary directories
under `.build/environment-tests/tmp`, and writes `results.xml` alongside the
binary. Its dependencies come from the pinned checkout's clang and GoogleTest.
The production core uses the C++ standard library and Linux/POSIX calls only;
the test runner links the host C++ standard library.

On 2026-09-08, compilation passed with clang 23.0.0git, LLVM revision
`53d18800eda3b7407e53366f27ca78e922c6e0db`, C++20, `-fno-exceptions`,
`-fno-rtti`, and `-Wall -Wextra -Werror`. All 22 GoogleTests passed, covering
expansion, path/component byte limits, UTF-8 and traversal rejection, lexical aliases/nesting, symlinks,
unsafe ownership/permissions, regular files, private creation, no mutation on
invalid input, unchanged unrelated permissions, handle lifetime, and 12
simultaneous preparations converging on one directory inode.

These are standalone core tests, not evidence that the browser can yet select,
activate, or isolate named environments.
