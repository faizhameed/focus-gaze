/**
 * focusGaze Bridge service worker.
 * - Forwards active-tab URL events to the local desktop app.
 * - Accepts one-time pairing messages from the app's localhost pair page
 *   (chrome.runtime.onMessageExternal + externally_connectable).
 */

const DEFAULT_PORT = 18765;
const DEFAULT_TOKEN = "";

/**
 * Load bridge config from synced extension storage.
 * @returns {Promise<{port:number, token:string, enabled:boolean}>}
 */
async function getConfig() {
  return chrome.storage.sync.get({
    port: DEFAULT_PORT,
    token: DEFAULT_TOKEN,
    enabled: true,
  });
}

/**
 * POST a browser event to the local focusGaze bridge.
 * @param {object} payload
 */
async function postEvent(payload) {
  const { port, token, enabled } = await getConfig();
  if (!enabled || !token) {
    return;
  }
  const url = `http://127.0.0.1:${port}/v1/url`;
  try {
    await fetch(url, {
      method: "POST",
      headers: {
        "Content-Type": "application/json",
        Authorization: `Bearer ${token}`,
        "X-FocusGaze-Token": token,
      },
      body: JSON.stringify(payload),
    });
  } catch (e) {
    console.debug("focusGaze bridge post failed", e);
  }
}

/**
 * @param {chrome.tabs.Tab} tab
 * @param {string} event
 */
function tabPayload(tab, event) {
  return {
    url: tab.url || "",
    title: tab.title || "",
    tabId: String(tab.id),
    browser: "chrome",
    event,
    ts: Math.floor(Date.now() / 1000),
  };
}

chrome.tabs.onActivated.addListener(async (activeInfo) => {
  try {
    const tab = await chrome.tabs.get(activeInfo.tabId);
    if (tab) {
      await postEvent(tabPayload(tab, "activated"));
    }
  } catch (_) {}
});

chrome.tabs.onUpdated.addListener(async (tabId, changeInfo, tab) => {
  if (changeInfo.status === "complete" || changeInfo.url) {
    await postEvent(tabPayload(tab, "updated"));
  }
});

chrome.tabs.onRemoved.addListener(async (tabId) => {
  await postEvent({
    tabId: String(tabId),
    browser: "chrome",
    event: "closed",
    ts: Math.floor(Date.now() / 1000),
  });
});

/**
 * Pairing from desktop app pair page (http://127.0.0.1:…/v1/pair-ui).
 * Message shape: { type: "focusgaze.pair", token: string, port?: number }
 */
chrome.runtime.onMessageExternal.addListener((message, sender, sendResponse) => {
  const url = sender?.url || "";
  const fromLoopback =
    url.startsWith("http://127.0.0.1:") ||
    url.startsWith("http://localhost:") ||
    url.startsWith("http://[::1]:");
  if (!fromLoopback) {
    sendResponse({ ok: false, error: "forbidden_origin" });
    return false;
  }
  if (!message || message.type !== "focusgaze.pair") {
    sendResponse({ ok: false, error: "unknown_message" });
    return false;
  }
  const token = typeof message.token === "string" ? message.token.trim() : "";
  const port = Number(message.port) || DEFAULT_PORT;
  if (!token) {
    sendResponse({ ok: false, error: "missing_token" });
    return false;
  }
  chrome.storage.sync
    .set({ token, port, enabled: true })
    .then(() => {
      console.info("focusGaze paired with desktop app");
      sendResponse({ ok: true, port });
    })
    .catch((err) => {
      sendResponse({ ok: false, error: String(err) });
    });
  return true; // async sendResponse
});
