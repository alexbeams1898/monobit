// Boot + toolbar wiring.
import { S, uniqueId, markDirty } from "./state.js";
import { buildGraph, autoLayout, fitView } from "./graph.js";
import { render, setEdgeClickHandler } from "./render.js";
import { initPanel, openPanel, closePanel, openEdgePanel } from "./panel.js";
import { initInteractions } from "./interact.js";
import * as api from "./api.js";

const $ = (id) => document.getElementById(id);

async function loadAll() {
  try {
    await api.load();
    ["addEnc", "addThought", "simBtn", "layoutBtn"].forEach(id => $(id).disabled = false);
    const g = buildGraph();
    if (!Object.keys(S.layout).length) autoLayout(g);
    const wrap = $("canvasWrap");
    fitView(g, wrap.clientWidth, wrap.clientHeight);
    closePanel();
    render();
  } catch (e) {
    $("status").textContent = "not connected -- run tools/tree.bat (this page is served by it)";
  }
}

initPanel(render);
setEdgeClickHandler(openEdgePanel);
initInteractions();
loadAll();

$("legendBtn").onclick = () => $("legend").classList.toggle("open");

$("reloadBtn").onclick = () => {
  if (S.dirty && !confirm("Discard unsaved changes and reload from disk?")) return;
  loadAll();
};
$("saveBtn").onclick = async () => { await api.save(); render(); };
$("addEnc").onclick = () => {
  const id = uniqueId("new_encounter");
  S.doc.encounters.push({ id, value: 1, tiers: [{ text: "" }] });
  S.layout["e:" + id] = { x: (80 - S.view.x) / S.view.k, y: (80 - S.view.y) / S.view.k };
  markDirty(); render(); openPanel("e:" + id);
};
$("addThought").onclick = () => {
  const id = uniqueId("new_thought");
  S.doc.thoughts.push({ id, faculty: "perception", text: "" });
  S.layout["t:" + id] = { x: (80 - S.view.x) / S.view.k, y: (140 - S.view.y) / S.view.k };
  markDirty(); render(); openPanel("t:" + id);
};
$("simBtn").onclick = () => {
  S.simOn = !S.simOn;
  $("simBtn").className = S.simOn ? "on" : "";
  closePanel();
  render();
};
$("layoutBtn").onclick = () => {
  const g = buildGraph();
  autoLayout(g);
  const wrap = $("canvasWrap");
  fitView(g, wrap.clientWidth, wrap.clientHeight);
  markDirty();
  render();
};
$("search").oninput = () => render();
window.addEventListener("beforeunload", (ev) => { if (S.dirty) ev.preventDefault(); });
