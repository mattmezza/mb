# Security and local-data status

The built product keeps upstream security controls enabled. No sandbox, site
isolation or Safe Browsing feature has been disabled to reduce dependencies.
The actual Xorg launch verified namespace and seccomp sandboxing; see the
[product baseline](product-baseline-2026-09-08.md). Other runtime behavior remains
**pending** where noted.

## Source-backed baseline

- On Linux x86_64, upstream enables the seccomp-BPF sandbox by default.
  Chromium's build file explicitly says not to disable it without consulting
  Chromium security.
- Desktop Chromium has default site isolation. The source describes the
  disabling switch as disabling the default desktop isolation shipped since
  M67. It is not part of the planned launch command.
- The Safe Browsing service and its database/UI managers are present. Several
  Safe Browsing and download/password-protection request paths obtain the
  Google API key; the unbranded defaults leave that key unset. Some source
  paths can form requests without a key, but source inspection alone cannot
  establish server acceptance, protection level, quota, or user-visible
  behavior. No unofficial key will be added.
- The profile password store uses Chromium's local `LoginDatabase` backend and
  `OSCryptAsync`. Linux source includes freedesktop Secret Service and XDG
  Secret Portal key providers, including explicit failure states when those
  services are unavailable. This supports integration with a desktop secret
  service; it does not prove that a particular Arch/X11 session will encrypt
  or retrieve saved credentials successfully.

## Pending runtime checks

Separate environment roots isolate browser databases; they are not separate OS
accounts or a promise of distinct encryption keys. The pinned Linux Freedesktop
provider uses an OS-wide application attribute or KWallet folder/key. Its display
name argument does not change those lookup identifiers. Product integration must
give those identifiers a stable product namespace through the existing provider,
without changing encryption algorithms or accessing installed Chromium entries.
Changing persistent lookup identifiers after credentials exist requires migration.

The Secret Portal provider stores token/status preferences in root-local
`Local State`, but the portal supplies the secret and exposes no product-identity
argument in this Chromium API. Separate roots therefore do not establish separate
portal secrets. At this pin portal encryption is disabled by default while its
decryption provider remains enabled. The lower-priority POSIX provider retains
Chromium's fixed compatibility key. These upstream behaviors have not been
changed; actual keyring/portal behavior and product identity are unverified.

- `chrome://sandbox` and a normal multi-process launch confirm the effective
  Linux sandbox configuration.
- Cross-site frame/navigation behavior confirms effective site isolation with
  the actual command line and policy state.
- Standard and enhanced Safe Browsing state, lookup/report behavior, and all
  Google-dependent security-service responses without supplied keys.
- Password save, decrypt-after-restart, keyring/portal availability, and
  Incognito non-persistence.
- Media playback/codec behavior, extension interactions, and all product
  integration behavior.

## Sources

- Pinned Chromium `d04cdb24d67b081f6cf80200ffc5233f44b61109`:
  `google_apis/config.gni` and `google_apis/default_api_keys-inc.cc`;
  `sandbox/features.gni`; `content/public/common/content_switches.cc`;
  `chrome/browser/safe_browsing/safe_browsing_service.h`;
  `components/safe_browsing/core/browser/db/v4_protocol_config.cc` and
  `db/v5_search_hashes_util.cc`; `chrome/browser/password_manager/factories/profile_password_store_factory.cc` and `password_store_backend_factory.cc`; `components/os_crypt/async/browser/freedesktop_secret_key_provider.cc`; and `secret_portal_key_provider.cc`.
- [Chromium API keys documentation](https://www.chromium.org/developers/how-tos/api-keys/) explains the ownership and service restrictions that apply to any deliberately configured keys.

## Product parser compiler integration

Product C++ is compiled with Chromium's normal buffer diagnostics and runtime
hardening. The C runtime argc/argv boundary uses a single documented conversion
to std::span; subsequent access is bounded. The vendored toml++ header has a
scoped `-Wunsafe-buffer-usage` diagnostic exception because its pointer-based
implementation has not adopted Chromium's span migration. This is a static
warning exception confined to that include, not a disabled sandbox, sanitizer
or bounds check. The parser's 1 MiB input cap and value/key depth limits remain
active and covered by tests. It does not establish that every vendor buffer
access is audited; continued vendor updates and malformed-input testing remain
part of maintenance. No target-wide buffer-warning suppression is added.

## Bundled new-tab page

The Linux product NTP uses compiled local HTML/CSS and no remote browser-UI
scripts. Its CSP denies script, connection, frame and worker sources. The
`NewTabPageLocation` policy override is deliberately unsupported because it can
replace this UI with a remote document before normal URL rewriting. Search
engine selection, native OTR routing and legitimate extension NTP overrides
retain their upstream paths. See [local NTP integration](local-new-tab.md) for
scope and pending validation. No browsing-origin or certificate policy is relaxed.

## Linux inherited UI omissions (pending runtime gate)

The AI Mode omnibox action, AI placeholder/hint and AI Ctrl+Enter shortcut are
omitted from the Linux product UI. Ordinary native omnibox handling remains.
Unsupported Customize Chrome NTP customization entry points are also omitted
because the product owns its compiled NTP. These changes remove UI entry points;
they do not claim that every upstream AI-related service is disabled. They do
not alter Blink, networking, sandboxing, certificate validation, permissions,
Safe Browsing or extension APIs. No remote UI code or replacement service is
introduced. The native profile-settings customization command remains.

## Early environment selection

The Linux gate reads explicit TOML after Chromium's web-security and pipe-FD
checks, validates every configured root and startup URL, and prepares only the
selected private root. Early URL validation uses stateless Chromium parsers and
canonicalizers; GURL cannot run before Content registers schemes because it
marks the global registry used. A regression test verifies later registration.

Patch 0009 rejects native user-data fallback when a product environment is
installed. A deterministic startup probe changes its own prepared path into a
regular file and verifies exit13 with the default root absent. Chromium's native
singleton remains responsible for process locking. An open directory descriptor
does not protect against arbitrary path replacement by other processes running
as the same OS user.
