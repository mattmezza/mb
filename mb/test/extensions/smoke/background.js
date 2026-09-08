"use strict";

const DEFAULT_STATE = Object.freeze({
  contentContextCount: 0,
  lastContentIncognito: false,
  popupPingCount: 0,
  fixtureValue: "not saved",
});

async function state() {
  return chrome.storage.local.get(DEFAULT_STATE);
}

async function recordContentContext(incognito) {
  const previous = await state();
  const next = {
    contentContextCount: previous.contentContextCount + 1,
    lastContentIncognito: incognito,
  };
  await chrome.storage.local.set(next);
  return {...previous, ...next};
}

async function recordPopupPing() {
  const previous = await state();
  const next = {popupPingCount: previous.popupPingCount + 1};
  await chrome.storage.local.set(next);
  return {...previous, ...next};
}

chrome.runtime.onMessage.addListener((message, sender, sendResponse) => {
  if (!message || typeof message.type !== "string") {
    return false;
  }
  let operation;
  if (message.type === "fixture:content-context") {
    // No URLs, titles, history, page bodies, or browsing metadata are read.
    operation = recordContentContext(sender.tab?.incognito === true).then((next) => ({
      ok: true,
      incognito: next.lastContentIncognito,
      contentContextCount: next.contentContextCount,
    }));
  } else if (message.type === "fixture:popup-ping") {
    operation = recordPopupPing().then((next) => ({ok: true, popupPingCount: next.popupPingCount}));
  } else if (message.type === "fixture:save-value") {
    operation = chrome.storage.local.set({fixtureValue: "saved"}).then(state).then((next) => ({
      ok: true,
      fixtureValue: next.fixtureValue,
    }));
  } else if (message.type === "fixture:get-state") {
    operation = state().then((next) => ({ok: true, state: next}));
  } else {
    return false;
  }
  operation.then(sendResponse).catch((error) => sendResponse({ok: false, error: String(error)}));
  return true;
});
