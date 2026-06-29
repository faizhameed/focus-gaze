async function load() {
  const data = await chrome.storage.sync.get({
    port: 18765,
    token: "",
    enabled: true,
  });
  document.getElementById("port").value = data.port;
  document.getElementById("token").value = data.token;
  document.getElementById("enabled").checked = data.enabled;
}

document.getElementById("save").addEventListener("click", async () => {
  const port = Number(document.getElementById("port").value) || 18765;
  const token = document.getElementById("token").value.trim();
  const enabled = document.getElementById("enabled").checked;
  await chrome.storage.sync.set({ port, token, enabled });
  window.close();
});

load();
