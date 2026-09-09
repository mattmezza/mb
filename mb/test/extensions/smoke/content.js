"use strict";

function notifyTest(message) {
  if (chrome.test?.sendMessage) {
    chrome.test.sendMessage(message);
  }
}

(() => {
  const status = document.createElement("aside");
  status.id = "browser-capability-fixture-status";
  status.setAttribute("role", "status");
  status.style.cssText = [
    "position:fixed", "right:12px", "bottom:12px", "z-index:2147483647",
    "max-width:300px", "padding:8px 10px", "border:1px solid #334155",
    "border-radius:6px", "background:#f8fafc", "color:#0f172a",
    "font:13px/1.3 sans-serif",
  ].join(";");
  status.textContent = "Browser capability fixture: contacting service worker…";
  document.documentElement.append(status);
  chrome.runtime.sendMessage({type: "fixture:content-context"}, (response) => {
    if (chrome.runtime.lastError || !response?.ok) {
      status.textContent = "Browser capability fixture: service worker response failed.";
      return;
    }
    const mode = response.incognito ? "incognito" : "regular";
    status.textContent = `Browser capability fixture: ${mode} context; service worker responded; local count ${response.contentContextCount}.`;
    notifyTest(`content:${response.contentContextCount}`);
  });
})();
