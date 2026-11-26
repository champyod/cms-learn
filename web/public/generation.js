// The generation panel is pushed: this opens the stream the page names, writes what it carries into the
// panel, and lets the browser reopen it when the server closes — the reopening is what keeps it live.
(() => {
  const panel = document.getElementById("generation-live");
  if (!panel || !window.EventSource) return;
  const events = new EventSource(panel.dataset.stream);
  events.addEventListener("state", (event) => {
    const state = JSON.parse(event.data);
    const line = document.createElement("span");
    line.className = "font-mono";
    line.textContent = state.steps_done + " · " + state.steps_line;
    panel.replaceChildren(line);
    if (state.has_refusal) {
      const refusal = document.createElement("p");
      refusal.className = "text-sm text-error";
      refusal.textContent = state.refusal;
      panel.append(refusal);
    }
  });
})();
