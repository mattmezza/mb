# Security and local-data status

The planned build keeps upstream security controls enabled. No sandbox, site
isolation, Safe Browsing, or browser feature has been disabled to reduce build
dependencies. The product integration and browser binary do not yet exist, so
runtime behavior remains **pending** where noted.

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
