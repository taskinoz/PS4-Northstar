"use strict";
// The helper's page. Everything runs in the app (src-tauri/src/gui.rs): this
// polls get_state and sends the player's choices with sign_in and retry.

const invoke = window.__TAURI__ ? window.__TAURI__.core.invoke : null;
const $ = (id) => document.getElementById(id);
let form = -1, lastSeq = 0;

function setStatus(message) {
  $("ea").className = "status " + (message.kind || "");
  $("ea-text").textContent = message.text || "";
}

function setResult(message) {
  $("result").className = message.kind || "";
  $("result").textContent = message.text || "";
}

function mode() {
  const checked = document.querySelector("input[name=mode]:checked");
  return checked ? checked.value : "local";
}

function showFields() { $("fields").hidden = mode() !== "remote"; }

function render(state) {
  $("version").textContent = "NorthstarPS4 Token Helper " + state.version;
  setStatus(state.ea);
  $("retry").hidden = !state.retry;
  $("retry").disabled = state.busy;
  if (state.form !== form) {
    form = state.form;
    document.querySelector(`input[name=mode][value=${state.mode === "remote" ? "remote" : "local"}]`).checked = true;
    if (state.address) $("address").value = state.address;
    showFields();
  }
  showFields(); // also when the choice changed without a change event (assistive tools)
  $("submit").disabled = !state.ready || state.busy;
  setResult(state.result);
  const list = $("activity");
  const atBottom = list.scrollTop + list.clientHeight >= list.scrollHeight - 4;
  for (const event of state.events) {
    const item = document.createElement("li");
    item.textContent = event.text;
    list.appendChild(item);
    lastSeq = event.seq;
  }
  while (list.children.length > 300) list.firstChild.remove();
  if (atBottom) list.scrollTop = list.scrollHeight;
  document.body.style.cursor = state.busy ? "progress" : "";
}

async function poll() {
  try {
    render(await invoke("get_state", { since: lastSeq }));
  } catch (error) {
    setResult({ text: "Something went wrong: " + error, kind: "error" });
  }
  setTimeout(poll, 500);
}

for (const radio of document.querySelectorAll("input[name=mode]")) radio.addEventListener("change", showFields);
$("code").addEventListener("input", () => { $("code").value = $("code").value.replace(/\D/g, "").slice(0, 4); });
$("signin").addEventListener("submit", async (event) => {
  event.preventDefault();
  $("submit").disabled = true;
  await invoke("sign_in", { mode: mode(), address: $("address").value, code: $("code").value });
  if (mode() === "remote") $("code").value = "";
});
$("retry").addEventListener("click", () => invoke("retry"));

if (invoke) {
  poll();
} else {
  setStatus({ text: "Open the NorthstarPS4 Token Helper app; this page only works inside it.", kind: "error" });
}
