# Updating the Chromium baseline

Status: the initial pinned Chromium build and sandboxed Xorg baseline passed.
Product overlay application, GN generation and focused product tests work; the
renamed browser is compiling. A complete upstream-update and package rehearsal
remains pending. This document
defines the maintenance procedure; it does not establish release acceptance.

## Version and source authority

`mb/upstream-version.toml` is the committed authority for the Chromium version,
exact commit, tag, release branch, selection date and official provenance. The
same file pins depot_tools independently. A Chromium release number alone is
insufficient: the source tag must resolve to the recorded commit, and `DEPS`
from that commit determines the dependency/toolchain revisions.

Use the official [ChromiumDash Linux stable feed](https://chromiumdash.appspot.com/fetch_releases?channel=Stable&platform=Linux&num=1)
and [Chromium source tags](https://chromium.googlesource.com/chromium/src/+refs)
when selecting a candidate. Review its official
[Chrome Releases announcement](https://chromereleases.googleblog.com/search/label/Stable%20updates)
for security fixes and rollout context. The release branch identifies the
maintenance line; build the immutable tag/commit, not the moving branch head.
Do not substitute the Windows or macOS release when platform versions differ.

Check the current release without changing the pin or checkout:

```sh
python3 mb/tools/check_upstream.py
```

This emits a TOML candidate record to standard output and the current-versus-
candidate comparison to standard error. It cross-checks ChromiumDash's exact
commit against the official Gitiles tag. A disagreement or malformed response
fails the command. The emitted depot_tools revision is retained from the current
manifest and explicitly marked as unvalidated against the candidate; it is not
an automatic depot_tools recommendation. Review the record before transferring
selected values into the authoritative manifest. The checker does not invent
an announcement URL or overwrite the recorded announcement.

The live check on 2026-09-08 returned the currently pinned Linux stable version,
152.0.7977.82, at `d04cdb24d67b081f6cf80200ffc5233f44b61109`. This is a
selection-time result, not a claim that a moving feed will always return it.

## Preserve the working baseline

An update is a separate reviewed change in this product repository. Keep the old
pin, integration changes, build receipts and test evidence available throughout
the update. Preserve the working browser package until the replacement passes
release tests. Back up user environments before allowing a newer browser to
migrate them; Chromium data migrations are not generally reversible by running
an older executable. Use fresh test environments for the update rehearsal.

The current bootstrap intentionally refuses to move an existing checkout's HEAD
or overwrite local changes. Do not bypass that guard using `git reset --hard`,
`git clean`, forced checkout, deleting lock files or an unreviewed dependency
resynchronization. A changed manifest does not update an existing source tree.

For now, prepare a separate product checkout and its own ignored `.build/`
directory for a candidate. Check disk/RAM first: a second source tree and both
debug/release outputs need additional capacity. A Git worktree for the product
repository is suitable, but a Chromium worktree alone does not independently
manage nested gclient dependencies and downloaded tools. Run each checkout's
bootstrap from its own root. Do not share mutable output or dependency trees
between old and new versions.

## Candidate validation sequence

1. Record the candidate's Linux stable metadata, resolve its exact source tag,
   and review the upstream release announcement. Record the UTC selection date.
   Check the candidate's documented depot_tools/toolchain requirements before
   retaining or changing the separately pinned depot_tools commit.
2. In the candidate product checkout, review and change the manifest. Run
   `python3 mb/tools/bootstrap.py doctor` and install any newly required official
   Arch dependencies through the documented user-operated package procedure.
3. Run `python3 mb/tools/bootstrap.py fetch`, then
   `python3 mb/tools/upstream.py hooks` and
   `python3 mb/tools/upstream.py gen`. These commands require clean source,
   verify the exact revisions, and retain the baseline arguments. Inspect new
   hooks and GN argument changes before running them. Unused GN arguments fail
   generation rather than silently changing the intended build.
4. Record the active command in `STATUS.md`, then run
   `python3 mb/tools/upstream.py build --jobs 4`, adjusting parallelism only from
   measured resources. Retain the timestamped log and successful receipt. Run
   `upstream_smoke.py --build-receipt PATH` on the real Xorg session and inspect
   its screenshots and sandbox evidence as described in [testing](testing.md).
5. After that baseline passes, regenerate branding inputs and port the narrow
   product integration in small compiled steps. Run the standalone tests,
   focused browser tests and environment/incognito/capability checks. Complete
   a release build and installed Arch-package smoke test before changing the
   supported version. Use `product.py prepare`, `gen`, `test`, and `build` as documented in
   [product integration](product-integration.md). Packaging commands remain pending.

## Reviewing integration conflicts

Product code belongs under `//mb/`; changes to upstream files must have an
enumerated purpose and an integration record. The initial branding patch is
`mb/patches/0001-product-branding.patch`, applied by the guarded product workflow.
It has passed GN and focused product tests; its browser build is still underway.
There is no unattended rebase command. A passing standalone test does not prove
that the browser integration has been ported.

For each affected upstream file, compare the old and candidate versions before
reapplying the product change. Identify renamed classes, moved initialization
steps, changed ownership/lifetime rules, updated GN dependencies and changed
resource inputs. Resolve one integration area at a time and compile its affected
targets. Do not resolve a conflict by replacing the candidate file with the old
fork's copy; that can discard unrelated fixes, including security changes.

Pay particular attention to these contracts:

- Browser startup must select a user-data root before Chromium initializes its
  profile paths and process singleton. Preserve child-process dispatch and the
  original process-argument backing memory used for Linux process titles.
- Tabs remain owned by Chromium's tab model. Native tab-collection observation,
  window layout, session restoration and drag/drop lifetimes must still agree.
- Branding regeneration must preserve resource IDs, substitutions, upstream
  attribution and required license credits. Changed generated inputs require
  rerunning the corresponding generator and its tests.
- Password storage, permissions, off-the-record behavior and sandbox setup must
  retain upstream security semantics. Investigate API changes instead of
  disabling a feature to make the patch compile.

Keep the source diff narrow and review generated changes separately from their
authoritative inputs. Record any intentionally disabled feature and its security
effect in [security](security.md). A patch that applies without conflicts still
needs build and runtime validation.

## Release and rollback boundary

Updates are delivered by replacing the Arch package. There is no product
automatic updater. Publish a replacement only after release-mode tests pass,
with the source/product revisions, package checksum, test environment and known
limitations recorded. Retaining an older package helps reproduce regressions;
it does not make downgrading a migrated user-data root safe. Restore a suitable
backup or use a fresh root when testing an older executable.
