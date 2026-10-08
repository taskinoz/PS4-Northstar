"use strict";
// The helper's page. Everything runs in the app (src-tauri/src/gui.rs): this
// polls get_state and sends the player's choices.

const invoke = window.__TAURI__ ? window.__TAURI__.core.invoke : null;
const $ = (id) => document.getElementById(id);
let form = -1, lastSeq = 0, installSeq = 0, tabChosen = false;

// ---- Tabs ----

function showTab(name, byPlayer) {
  for (const tab of ["install", "signin"]) {
    $("tab-" + tab).setAttribute("aria-selected", String(tab === name));
    $("panel-" + tab).hidden = tab !== name;
  }
  if (byPlayer) tabChosen = true;
}
$("tab-install").addEventListener("click", () => showTab("install", true));
$("tab-signin").addEventListener("click", () => showTab("signin", true));

// ---- Shared ----

function setMessage(element, message, statusDot) {
  const kind = message.kind || "";
  if (statusDot) {
    element.className = "status " + kind;
    element.querySelector("span:last-child").textContent = message.text || "";
  } else {
    element.className = kind;
    element.textContent = message.text || "";
  }
}

function appendLog(list, events) {
  const atBottom = list.scrollTop + list.clientHeight >= list.scrollHeight - 4;
  let last = 0;
  for (const event of events) {
    const item = document.createElement("li");
    item.textContent = event.text;
    list.appendChild(item);
    last = event.seq;
  }
  while (list.children.length > 300) list.firstChild.remove();
  if (atBottom) list.scrollTop = list.scrollHeight;
  return last;
}

// ---- Install ----

function renderInstall(install, installEvents) {
  $("folder").textContent = install.folder || "No folder chosen";
  if (install.status && install.status.text) setMessage($("folder-status"), install.status, true);
  const latest = install.latest ? `Latest release: ${install.latest}.` : "Looking for the latest release…";
  const installed = install.info && install.info.installedVersion ? ` Installed: ${install.info.installedVersion}.` : "";
  $("latest").textContent = latest + installed;
  $("install-label").textContent = install.busy ? "Working…" : (install.action || "Install");
  $("install").disabled = install.busy || !install.action;
  $("check").disabled = install.busy;
  $("choose").disabled = install.busy;
  $("uninstall").disabled = install.busy || !install.canUninstall;
  $("uninstall-box").hidden = !install.canUninstall && !install.busy;
  setMessage($("install-result"), install.result, false);
  const seq = appendLog($("install-log"), installEvents);
  if (seq) installSeq = seq;
}

$("choose").addEventListener("click", () => invoke("choose_game_folder").catch(() => {}));
$("check").addEventListener("click", () => invoke("check_install"));
$("install").addEventListener("click", () => invoke("install_now"));
$("uninstall").addEventListener("click", () => invoke("uninstall_now", { removeMods: $("remove-mods").checked }));

// ---- Sign in ----

function mode() {
  const checked = document.querySelector("input[name=mode]:checked");
  return checked ? checked.value : "local";
}

// A console paired with this computer signs in with its key; a code is only for
// a new one. The game says whether it is paired; one too old to say counts as
// paired when it is the console this helper signed in last.
let paired = "", checked = { address: "", status: null }, checkTimer = 0;
function isPaired() {
  const address = $("address").value.trim();
  const status = checked.address === address ? checked.status : null;
  if (status && status.found && status.paired !== null) return status.paired;
  return paired !== "" && address === paired;
}

function showFields() {
  $("fields").hidden = mode() !== "remote";
  const pairedHere = isPaired();
  $("code-field").hidden = pairedHere;
  $("paired-note").hidden = !pairedHere;
  $("fields").classList.toggle("paired", pairedHere);
}

function checkConsole() {
  clearTimeout(checkTimer);
  const address = $("address").value.trim();
  if (mode() !== "remote" || !address || checked.address === address) return;
  checkTimer = setTimeout(async () => {
    try {
      const status = await invoke("check_console", { address });
      if ($("address").value.trim() === address) {
        checked = { address, status };
        showFields();
      }
    } catch (_) { /* the box stays as it was */ }
  }, 400);
}

// The account ID stays hidden unless asked for, so a screenshot of this
// window does not share it by accident.
let accountShown = false, lastAccount = "";
function showAccount(account) {
  lastAccount = account;
  $("show-account").hidden = !account;
  $("show-account").textContent = accountShown ? "Hide account ID" : "Show account ID";
  $("show-account").setAttribute("aria-pressed", String(accountShown));
  $("account-row").hidden = !account || !accountShown;
  $("account-id").textContent = accountShown ? account : "";
}

function renderSignIn(state) {
  setMessage($("ea"), state.ea, true);
  $("retry").hidden = !state.retry;
  $("retry").disabled = state.busy;
  if ((state.paired || "") !== paired) {
    // A sign-in just paired a console: ask again rather than trust the last answer.
    paired = state.paired || "";
    checked = { address: "", status: null };
    checkConsole();
  }
  showAccount(state.account || "");
  if (state.form !== form) {
    form = state.form;
    document.querySelector(`input[name=mode][value=${state.mode === "remote" ? "remote" : "local"}]`).checked = true;
    if (state.address) $("address").value = state.address;
    checked = { address: "", status: null }; // the helper may have just paired or unpaired it
    checkConsole();
  }
  showFields(); // also when the choice changed without a change event (assistive tools)
  $("submit").disabled = !state.ready || state.busy;
  setMessage($("result"), state.result, false);
  const seq = appendLog($("activity"), state.events);
  if (seq) lastSeq = seq;
}

for (const radio of document.querySelectorAll("input[name=mode]"))
  radio.addEventListener("change", () => { showFields(); checkConsole(); });
$("address").addEventListener("input", () => { showFields(); checkConsole(); });
$("show-account").addEventListener("click", () => { accountShown = !accountShown; showAccount(lastAccount); });
$("code").addEventListener("input", () => { $("code").value = $("code").value.replace(/\D/g, "").slice(0, 4); });
$("signin").addEventListener("submit", async (event) => {
  event.preventDefault();
  $("submit").disabled = true;
  await invoke("sign_in", { mode: mode(), address: $("address").value, code: $("code").value });
  if (mode() === "remote") $("code").value = "";
});
$("retry").addEventListener("click", () => invoke("retry"));

// ---- Polling ----

function render(state) {
  $("version").textContent = "NorthstarPS4 Token Helper " + state.version;
  // Start on Install until PS4 Northstar is installed in the chosen folder.
  if (!tabChosen && state.install.info) {
    const installed = state.install.info.runtimeInstalled;
    showTab(installed ? "signin" : "install", false);
    tabChosen = true;
  }
  renderInstall(state.install, state.installEvents);
  renderSignIn(state);
  document.body.style.cursor = state.busy || state.install.busy ? "progress" : "";
}

async function poll() {
  try {
    render(await invoke("get_state", { since: lastSeq, installSince: installSeq }));
  } catch (error) {
    setMessage($("result"), { text: "Something went wrong: " + error, kind: "error" }, false);
  }
  setTimeout(poll, 500);
}

if (invoke) {
  poll();
} else {
  setMessage($("ea"), { text: "Open the NorthstarPS4 Token Helper app; this page only works inside it.", kind: "error" }, true);
}
