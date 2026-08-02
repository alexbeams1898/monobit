// Drawing: the SVG graph, the sim panel, validation strip, and the status line.
import { S, esc, trunc, encounters, thoughts, actionsOf, allFlagNames, allStatNames }
  from "./state.js"; // allFlagNames feeds the sim checklist + condition datalist
import { buildGraph, posOf, NODE_W, NODE_H, CHIP_GAP } from "./graph.js";
import { simKnowledge, condMet, simReset } from "./sim.js";
import { validate } from "./validate.js";

const $ = (id) => document.getElementById(id);

// Edge clicks open a panel owned by panel.js; injected to avoid a module cycle.
let onEdgeClick = () => {};
export function setEdgeClickHandler(fn) { onEdgeClick = fn; }

export function applyView() {
  $("viewport").setAttribute("transform",
    `translate(${S.view.x},${S.view.y}) scale(${S.view.k})`);
}

// The edge's stable identity (for the selected-edge highlight across re-renders).
export const edgeKey = (e) => `${e.from}>${e.to}:${e.cls}` + (e.info || "");

export function render() {
  const graph = buildGraph();
  const q = $("search").value.trim().toLowerCase();
  const matches = (n) => !q || (n.kind === "chip" ? n.label
    : n.data.id + " " + JSON.stringify(n.data)).toLowerCase().includes(q);
  const k = S.simOn ? simKnowledge() : null;

  applyView();

  // --- geometry pre-pass -----------------------------------------------------
  // Seats are PER FACE, not per direction: a node's left side hosts arriving
  // forward arrows AND departing backward hooks, so every endpoint touching a
  // face -- start or arrow -- gets its own seat there, ordered by where the far
  // end sits. The node grows to fit its busier face.
  const kindOf = {}, widthOf = {};
  graph.nodes.forEach(n => { kindOf[n.key] = n.kind; widthOf[n.key] = NODE_W[n.kind]; });

  // Pass 1: x-positions decide each edge's orientation and which faces it uses.
  const pos = {};
  graph.nodes.filter(n => n.kind !== "chip").forEach(n => pos[n.key] = { ...posOf(n.key) });
  graph.nodes.filter(n => n.kind === "chip").forEach(c => {
    pos[c.key] = { x: pos[c.parent].x + (NODE_W.enc - NODE_W.chip), y: 0 };
  });
  const faces = {};
  graph.edges.forEach(e => {
    // Three geometries: forward (right face -> left face), backward (left ->
    // right), and SIDE for same-column neighbors (right -> right, bowing out) --
    // a mutex tie between stacked scenes is the canonical case.
    const dx = pos[e.to].x - pos[e.from].x;
    e._orient = Math.abs(dx) < widthOf[e.from] * 0.75 ? "side"
                : dx >= 0 ? "fwd" : "back";
    const fFace = e._orient === "back" ? "L" : "R";
    const tFace = e._orient === "fwd" ? "L" : "R";
    (faces[e.from + "|" + fFace] ||= []).push({ e, start: true, far: e.to });
    (faces[e.to + "|" + tFace] ||= []).push({ e, start: false, far: e.from });
  });
  const nodeByKey = {};
  graph.nodes.forEach(n => nodeByKey[n.key] = n);
  const heightOf = (key) => {
    const seats = Math.max((faces[key + "|L"] || []).length, (faces[key + "|R"] || []).length);
    // A node carrying an unread-flag tag reserves a line for it.
    const base = NODE_H[kindOf[key] || "enc"] +
                 (nodeByKey[key] && nodeByKey[key].setsUnread ? 16 : 0);
    return Math.max(base, seats * 14 + 8);
  };

  // Pass 2: y-positions -- chips stack below their parent using DYNAMIC heights
  // (a grown parent pushes its chips down; a grown chip pushes the next one).
  graph.nodes.filter(n => n.kind === "chip")
    .sort((a, b) => a.chipIndex - b.chipIndex)
    .forEach(c => {
      const p = pos[c.parent];
      if (p._chipY == null) p._chipY = p.y + heightOf(c.parent) + CHIP_GAP;
      pos[c.key].y = p._chipY;
      p._chipY += heightOf(c.key) + CHIP_GAP;
    });

  // Pass 3: seat order by the far end's y, so lines fan without crossing at the face.
  Object.values(faces).forEach(list => list.sort((a, b) =>
    (pos[a.far] ? pos[a.far].y : 0) - (pos[b.far] ? pos[b.far].y : 0)));
  const seatY = (key, face, e, start) => {
    const list = faces[key + "|" + face];
    const i = list.findIndex(s => s.e === e && s.start === start);
    return pos[key].y + heightOf(key) * (i + 1) / (list.length + 1);
  };

  const edgesG = $("edges");
  edgesG.innerHTML = "";
  graph.edges.forEach((e, ei) => {
    const a = pos[e.from], b = pos[e.to];
    if (!a || !b) return;
    // Orientation-aware routing (see the face pre-pass): forward runs right ->
    // left, backward hooks left -> right, and SIDE edges (same column) bow out
    // past both right faces. Endpoints sit in their face's seats; the mid-lane
    // stagger fans long parallel runs into separate corridors.
    const fFace = e._orient === "back" ? "L" : "R";
    const tFace = e._orient === "fwd" ? "L" : "R";
    const x1 = fFace === "R" ? a.x + widthOf[e.from] : a.x;
    const x2 = tFace === "R" ? b.x + widthOf[e.to] : b.x;
    const y1 = seatY(e.from, fFace, e, true);
    const y2 = seatY(e.to, tFace, e, false);
    const mx = e._orient === "side"
      ? Math.max(x1, x2) + 46 + (ei % 4) * 10
      : (x1 + x2) / 2 + ((ei % 9) - 4) * 11;
    const d = `M${x1},${y1} C${mx},${y1} ${mx},${y2} ${x2},${y2}`;
    const p = document.createElementNS("http://www.w3.org/2000/svg", "path");
    p.setAttribute("d", d);
    p.setAttribute("class", "edge " + e.cls +
      (S.selectedEdge === edgeKey(e) ? " selected" : ""));
    const marker = { obs: "obs", plays: "plays", visible: "visible",
                     flagwire: "gateflag", linking: "obs" }[e.cls];
    if (marker)
      p.setAttribute("marker-end", `url(#arrow-${marker})`); // mutex ties are arrowless
    edgesG.appendChild(p);
    // A fat invisible twin makes the thin edge clickable; clicking explains the
    // relationship (which field creates this edge) in the side panel.
    const hit = document.createElementNS("http://www.w3.org/2000/svg", "path");
    hit.setAttribute("d", d);
    hit.setAttribute("class", "edge-hit");
    hit.addEventListener("click", (ev) => { ev.stopPropagation(); onEdgeClick(e); });
    edgesG.appendChild(hit);
  });

  const nodesG = $("nodes");
  nodesG.innerHTML = "";
  graph.nodes.forEach(n => {
    const pos_ = pos[n.key];
    const g = document.createElementNS("http://www.w3.org/2000/svg", "g");
    let cls = "node kind-" + n.kind;
    if (S.selected === n.key) cls += " selected";
    if (!matches(n)) cls += " dimmed";
    let lit = false, unreachable = false, badge = "";
    if (k) {
      if (n.kind === "enc") {
        const visible = condMet(n.data.visible_when, k);
        unreachable = !visible;
        lit = S.sim.observed.has(n.data.id);
        badge = visible ? (lit ? "observed" : "visible") : "hidden";
      } else if (n.kind === "thought" || n.kind === "remark") {
        const eligible = condMet(n.data.unlock_when, k);
        unreachable = !eligible && !S.sim.fired.has(n.data.id);
        lit = S.sim.fired.has(n.data.id);
        badge = lit ? (n.kind === "remark" ? "said" : "landed")
                    : (eligible ? "can land" : "locked");
      } else if (n.kind === "chip") {
        lit = condMet(n.cond, k);
        badge = lit ? "open" : "locked";
        unreachable = !lit;
      } else if (n.kind === "scene") {
        const done = n.data.set_flag && k.flags.has(n.data.set_flag);
        lit = done;
        badge = done ? "done" : (condMet(n.data.start_when, k) ? "armed" : "waiting");
      }
      if (unreachable) cls += " sim-unreachable";
      if (lit) cls += " sim-lit";
    }
    g.setAttribute("class", cls);
    g.setAttribute("transform", `translate(${pos_.x},${pos_.y})`);
    g.dataset.key = n.key;
    const w = NODE_W[n.kind], h = heightOf(n.key);
    const fill = n.kind === "thought" ? "var(--thought)"
      : n.kind === "remark" ? "var(--remark)"
      : n.kind === "scene" ? "var(--scene)"
      : n.kind === "chip" ? (n.sub === "tier" ? "var(--chip-tier)" : "var(--chip-act)")
      : (n.data.speaker ? "var(--enc-person)" : "var(--enc)");
    let inner = `<rect width="${w}" height="${h}" rx="${n.kind === "chip" ? 4 : 6}"
      fill="${fill}" stroke="#00000055"/>`;
    if (n.kind === "chip") {
      // A tier chip is what he can NOTICE; a deed chip is what he can DO.
      const tag = n.sub === "tier" ? "👁" : "✋";
      inner += `<text x="7" y="15" class="chiptext">${tag} ${esc(trunc(n.label, 19))}</text>`;
    } else if (n.kind === "thought") {
      inner += `<text x="10" y="17" class="id">${esc(trunc(n.data.id, 26))}</text>
        <text x="10" y="34">${esc(trunc(n.data.text, 24))}</text>`;
    } else if (n.kind === "remark") {
      // Said, not written: the voice (player by default) leads the label.
      const who = n.data.voice && n.data.voice !== "player" ? n.data.voice : "player";
      inner += `<text x="10" y="17" class="id">💬 ${esc(who)} · ${esc(trunc(n.data.id, 18))}</text>
        <text x="10" y="34">${esc(trunc(n.data.text, 24))}</text>`;
    } else if (n.kind === "scene") {
      inner += `<text x="10" y="18" class="id">🎬 ${esc(trunc(n.data.id, 22))}</text>
        <text x="10" y="35" class="id">${esc(n.data.level || "?")} · ${(n.data.steps || []).length} steps</text>`;
    } else {
      // Identity only: WHO/WHAT this is. The content lives on the chips below.
      const who = n.data.speaker ? (S.npcNames[n.data.speaker] || n.data.speaker) + " · " : "";
      const trig = n.data.trigger === "Enter" ? "⚡ " : "";
      inner += `<text x="10" y="18" class="id">${esc(trig)}${esc(who)}${esc(trunc(n.data.id, 24))}</text>
        <text x="10" y="35" class="id">${(n.data.tiers || []).length} tiers · ${actionsOf(n.data).length} deeds` +
        (n.data.speaker ? " · person" : "") + `</text>`;
    }
    if (badge) inner += `<text x="${w - 8}" y="${h - 7}" text-anchor="end" class="badge"
      fill="${lit ? "var(--sim-on)" : "#ffffff88"}">${esc(badge)}</text>`;
    // Flags this node sets that nothing reads yet -- a tag, not a floating node.
    if (n.setsUnread)
      inner += `<text x="10" y="${h - 7}" class="badge" fill="#8fbf9f">⚑ ${
        esc(trunc(n.setsUnread.join(", "), 24))}</text>`;
    g.innerHTML = inner;
    nodesG.appendChild(g);
  });

  const dl = document.querySelector("datalist#flagNames");
  if (dl) dl.innerHTML = allFlagNames().map(f => `<option>${esc(f)}</option>`).join("");

  const v = validate();
  const box = $("validation");
  box.className = v.length ? "open" : "";
  box.innerHTML = v.map(m => `<div class="vmsg ${m.lvl}">${esc(m.msg)}</div>`).join("");
  $("saveBtn").disabled = !S.dirty;
  $("status").textContent = (S.connected ? (S.dirty ? "unsaved changes" : "saved") : "not connected") +
    ` · ${encounters().length} encounters · ${thoughts().length} thoughts` +
    (v.length ? ` · ${v.filter(m => m.lvl === "err").length} errors` : "");
  renderSimPanel();
}

function renderSimPanel() {
  const p = $("simPanel");
  p.className = S.simOn ? "open" : "";
  if (!S.simOn) return;
  const stats = allStatNames();
  const flags = allFlagNames();
  p.innerHTML = "<h3>Simulate: stats</h3>" + (stats.length ? stats.map(s => `
    <div class="row"><span>${esc(s)}</span>
    <input type="number" class="statin" value="${S.sim.stats[s] || 0}" data-stat="${esc(s)}"></div>`).join("")
    : "<span class='dimtext'>no stat gates authored</span>") +
    "<h3 style=\"margin-top:8px\">flags</h3>" + (flags.length ? flags.map(f => `
    <label style="display:block"><input type="checkbox" data-flag="${esc(f)}"
      ${S.sim.flags.has(f) ? "checked" : ""}> ⚑ ${esc(f)}</label>`).join("")
    : "<span class='dimtext'>no flags authored</span>") +
    `<h3 style="margin-top:8px">click nodes to toggle</h3>
     <span class="dimtext">encounter = observed · thought = landed · remark = said
     (both auto-set their flag) · scene = completed · chips just show open/locked</span>
     <div style="margin-top:8px"><button class="small" id="simReset">Reset knowledge</button></div>`;
  p.querySelectorAll("input[data-stat]").forEach(inp => inp.onchange = () => {
    S.sim.stats[inp.dataset.stat] = Number(inp.value) || 0; render();
  });
  p.querySelectorAll("input[data-flag]").forEach(inp => inp.onchange = () => {
    const f = inp.dataset.flag;
    if (S.sim.flags.has(f)) S.sim.flags.delete(f); else S.sim.flags.add(f);
    render();
  });
  const r = p.querySelector("#simReset");
  if (r) r.onclick = () => { simReset(); render(); };
}
