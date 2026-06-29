const DEFAULT_PORT = 18765;
const DEFAULT_TOKEN = "";

async function getConfig() {
  const data = await chrome.storage.sync.get({
    port: DEFAULT_PORT,
    token: DEFAULT_TOKEN,
    enabled: true,
  });
  return data;
}

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
    // App may be offline; ignore.
    console.debug("focusGaze bridge post failed", e);
  }
}

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
