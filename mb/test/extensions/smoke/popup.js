"use strict";

const stateElement = document.querySelector("#state");
const resultElement = document.querySelector("#result");

function notifyTest(message) {
  if (chrome.test?.sendMessage) {
    chrome.test.sendMessage(message);
  }
}

function showResult(response) {
  resultElement.textContent = response?.ok
    ? "Service worker response: success."
    : "Service worker response: failed.";
}

function showState(response) {
  if (!response?.ok) {
    stateElement.textContent = "Local fixture state could not be read.";
    return;
  }
  const value = response.state;
  const mode = value.contentContextCount === 0
    ? "no loopback context reported"
    : value.lastContentIncognito ? "incognito" : "regular";
  stateElement.textContent = [
    `Last loopback context: ${mode}.`,
    `Local content count: ${value.contentContextCount}.`,
    `Local popup ping count: ${value.popupPingCount}.`,
    `Local fixture value: ${value.fixtureValue}.`,
  ].join(" ");
  notifyTest(`popup:state:${value.contentContextCount}:${value.fixtureValue}`);
}

function message(request, callback) {
  chrome.runtime.sendMessage(request, (response) => {
    callback(chrome.runtime.lastError ? {ok: false} : response);
  });
}

document.querySelector("#ping").addEventListener("click", () => {
  message({type: "fixture:popup-ping"}, (response) => {
    showResult(response);
    message({type: "fixture:get-state"}, (state) => {
      showState(state);
      if (state?.ok) {
        notifyTest(`popup:ping:${state.state.popupPingCount}`);
      }
    });
  });
});
document.querySelector("#save").addEventListener("click", () => {
  message({type: "fixture:save-value"}, (response) => {
    showResult(response);
    message({type: "fixture:get-state"}, (state) => {
      showState(state);
      if (state?.ok) {
        notifyTest("popup:saved");
      }
    });
  });
});
message({type: "fixture:get-state"}, showState);
