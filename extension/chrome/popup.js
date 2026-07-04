/**
 * focusGaze Bridge popup — settings, live bridge status, focus toggle.
 * Pairing is done from the desktop app (“Connect browser”) — no multi-profile installer.
 */

const DEFAULTS = { port: 18765, token: "", enabled: true };

/** @param {boolean} on */
function setToggle(el, on) {
  el.setAttribute("aria-checked", on ? "true" : "false");
}

/** @returns {boolean} */
function getToggle(el) {
  return el.getAttribute("aria-checked") === "true";
}

/**
 * Probe the local focusGaze bridge and update the status pill + focus toggle.
 * @param {number} port
 * @param {string} token
 */
async function refreshStatus(port, token) {
  const pill = document.getElementById("statusPill");
  const text = document.getElementById("statusText");
  const focusToggle = document.getElementById("focusToggle");

  pill.className = "pill pill-unknown";
  text.textContent = "Checking…";

  try {
    const health = await fetch(`http://127.0.0.1:${port}/v1/health`, {
      method: "GET",
      signal: AbortSignal.timeout(1200),
    });
    if (!health.ok) throw new Error("health failed");

    if (!token) {
      pill.className = "pill pill-offline";
      text.textContent = "No token";
      return;
    }

    const res = await fetch(`http://127.0.0.1:${port}/v1/status`, {
      method: "GET",
      headers: {
        Authorization: `Bearer ${token}`,
        "X-FocusGaze-Token": token,
      },
      signal: AbortSignal.timeout(1500),
    });
    if (res.status === 401) {
      pill.className = "pill pill-offline";
      text.textContent = "Bad token";
      return;
    }
    if (!res.ok) throw new Error("status failed");
    const data = await res.json();
    pill.className = "pill pill-online";
    text.textContent = "Bridge online";
    setToggle(focusToggle, !!data.focus_on);
    if (typeof data.camera_monitoring === "boolean") {
      const cam = document.getElementById("cameraToggle");
      setToggle(cam, data.camera_monitoring);
    }
  } catch (_) {
    pill.className = "pill pill-offline";
    text.textContent = "Offline";
  }
}

async function load() {
  const data = await chrome.storage.sync.get(DEFAULTS);
  document.getElementById("port").value = data.port;
  document.getElementById("token").value = data.token;
  document.getElementById("enabled").checked = data.enabled;
  await refreshStatus(Number(data.port) || DEFAULTS.port, data.token || "");
}

document.getElementById("toggleToken").addEventListener("click", () => {
  const input = document.getElementById("token");
  const btn = document.getElementById("toggleToken");
  const show = input.type === "password";
  input.type = show ? "text" : "password";
  btn.textContent = show ? "Hide" : "Show";
});

document.getElementById("save").addEventListener("click", async () => {
  const port = Number(document.getElementById("port").value) || DEFAULTS.port;
  const token = document.getElementById("token").value.trim();
  const enabled = document.getElementById("enabled").checked;
  await chrome.storage.sync.set({ port, token, enabled });
  await refreshStatus(port, token);
});

/**
 * Toggle Focus Mode via the desktop bridge (requires valid token + running app).
 */
document.getElementById("focusToggle").addEventListener("click", async () => {
  const el = document.getElementById("focusToggle");
  const port = Number(document.getElementById("port").value) || DEFAULTS.port;
  const token = document.getElementById("token").value.trim();
  if (!token) return;
  const next = !getToggle(el);
  try {
    const res = await fetch(`http://127.0.0.1:${port}/v1/focus`, {
      method: "POST",
      headers: {
        "Content-Type": "application/json",
        Authorization: `Bearer ${token}`,
        "X-FocusGaze-Token": token,
      },
      body: JSON.stringify({ on: next }),
      signal: AbortSignal.timeout(3000),
    });
    if (!res.ok) throw new Error("focus control failed");
    const data = await res.json();
    setToggle(el, !!data.focus_on);
    await refreshStatus(port, token);
  } catch (_) {
    await refreshStatus(port, token);
  }
});

load();
