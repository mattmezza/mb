# Smoke extension protocol

`smoke/` is an unpacked Manifest V3 manual fixture, not a product extension.
It has only the `storage` permission and injects only on `http://127.0.0.1/*`
and `http://localhost/*`. It makes no network request, includes no remote code,
and stores only counters, a fixed test value, and the last loopback page's
incognito boolean in `chrome.storage.local`. It does not read or retain URLs,
titles, history, or page contents.

Run a loopback page in a separate terminal:

```sh
python3 -m http.server 8000 --bind 127.0.0.1
```

In the browser's extensions page, enable developer mode, choose **Load
unpacked**, and select `mb/test/extensions/smoke`. Open
`http://127.0.0.1:8000/` and confirm the lower-right fixture status says that
the service worker responded and reports a regular context. Open the toolbar
action: its popup must display the local context count. Click **Ping service
worker** and confirm the visible success response and increased local popup
ping count. Click **Save local fixture value**, close the popup, reopen it, and
confirm that `saved` persists.

In the extension's details page, enable **Allow in incognito**, then open a new
incognito window at the loopback URL. The page status and popup's last loopback
context must report `incognito`. Disable the extension and reload the loopback
page: the status must no longer appear. Re-enable it and reload: the status
must reappear. Remove the extension, reload, and confirm it remains absent.

For the restart check, load the fixture again, save the fixture value, fully
exit the browser, relaunch it with the same test profile, and reopen the popup.
The local value should still be `saved`; if it is not, record the result as a
failure. Do not claim runtime success until these manual checks are recorded.
