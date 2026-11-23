// The verdict panel is pushed by the api, so this only opens the stream the page names and swaps the
// markup the server already rendered — no template logic lives here.
(() => {
  const panel = document.getElementById("verdict-panel");
  if (!panel || !window.EventSource) return;
  const events = new EventSource(panel.dataset.stream);
  events.addEventListener("verdict", (event) => {
    panel.innerHTML = event.data;
  });
})();
