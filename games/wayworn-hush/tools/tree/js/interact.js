// Mouse interactions: pan, zoom, node dragging, click-to-edit/toggle, and
// Shift+drag connecting (the gesture writes the FIELD; the edge follows the data).
import { S, encounters, thoughts, clauseList, obsOf, markDirty } from "./state.js";
import { buildGraph, posOf, parentOf, NODE_W } from "./graph.js";
import { simToggle } from "./sim.js";
import { openPanel } from "./panel.js";
import { render, applyView } from "./render.js";

const $ = (id) => document.getElementById(id);
let drag = null;

export function initInteractions() {
  const svg = $("svg");
  svg.addEventListener("mousedown", (ev) => {
    const nodeEl = ev.target.closest(".node");
    if (nodeEl && ev.shiftKey) {
      drag = { kind: "link", from: nodeEl.dataset.key,
               line: document.createElementNS("http://www.w3.org/2000/svg", "path") };
      drag.line.setAttribute("class", "edge linking");
      $("edges").appendChild(drag.line);
    } else if (nodeEl) {
      const dragKey = parentOf(nodeEl.dataset.key); // dragging a chip moves its parent
      const pos = posOf(dragKey);
      drag = { kind: "node", key: dragKey, clickKey: nodeEl.dataset.key,
               dx: ev.clientX / S.view.k - pos.x, dy: ev.clientY / S.view.k - pos.y,
               moved: false };
    } else {
      drag = { kind: "pan", sx: ev.clientX - S.view.x, sy: ev.clientY - S.view.y };
      svg.classList.add("panning");
    }
  });
  svg.addEventListener("mousemove", (ev) => {
    if (!drag) return;
    if (drag.kind === "pan") {
      S.view.x = ev.clientX - drag.sx; S.view.y = ev.clientY - drag.sy;
      applyView();
    } else if (drag.kind === "node") {
      const p = posOf(drag.key);
      p.x = ev.clientX / S.view.k - drag.dx; p.y = ev.clientY / S.view.k - drag.dy;
      drag.moved = true;
      render();
    } else if (drag.kind === "link") {
      const a = posOf(parentOf(drag.from));
      const mx = (ev.clientX - S.view.x) / S.view.k, my = (ev.clientY - S.view.y) / S.view.k;
      drag.line.setAttribute("d", `M${a.x + 90},${a.y + 20} L${mx},${my}`);
    }
  });
  svg.addEventListener("mouseup", (ev) => {
    svg.classList.remove("panning");
    if (!drag) return;
    if (drag.kind === "link") {
      drag.line.remove();
      const targetEl = ev.target.closest(".node");
      if (targetEl && parentOf(targetEl.dataset.key) !== parentOf(drag.from))
        connect(drag.from, targetEl.dataset.key);
    } else if (drag.kind === "node" && !drag.moved) {
      if (S.simOn) simToggle(parentOf(drag.clickKey)); else openPanel(drag.clickKey);
      render();
    } else if (drag.kind === "node" && drag.moved) {
      markDirty(); // layout sidecar rides the same save
      render();
    }
    drag = null;
  });
  svg.addEventListener("wheel", (ev) => {
    ev.preventDefault();
    const f = ev.deltaY < 0 ? 1.1 : 1 / 1.1;
    S.view.x = ev.clientX - (ev.clientX - S.view.x) * f;
    S.view.y = ev.clientY - (ev.clientY - S.view.y) * f;
    S.view.k *= f;
    applyView();
  }, { passive: false });
}

// Writing a connection. Targets may be CHIPS -- dropping on a gated tier/deed
// writes into THAT thing's own unlock condition, which is the whole point of
// chips: the arrow lands where the gate lives.
function connect(fromKey, toKey) {
  const src = parentOf(fromKey);
  const fk = src.slice(0, 1), fid = src.slice(2);
  const addObserved = (cond, id) => {
    const clauses = clauseList(cond);
    if (!clauses.length) return [{ observed: [id] }];
    const c = clauses[0];
    if (!obsOf(c).includes(id)) c.observed = obsOf(c).concat(id);
    return clauses;
  };
  const addFlag = (cond, name) => {
    const clauses = clauseList(cond);
    if (!clauses.length) return [{ flag: name }];
    if (!clauses[0].flag) { clauses[0].flag = name; return clauses; }
    clauses.push({ flag: name }); return clauses;
  };
  const applyTo = (cond, setter) => {
    if (fk === "e" || fk === "t") setter(addObserved(cond, fid));
    else if (fk === "f") setter(addFlag(cond, fid));
  };

  if (toKey.includes("#")) {
    // Chip target: the gated tier/action itself.
    const [pkey, sub] = toKey.split("#");
    const e = encounters().find(x => x.id === pkey.slice(2));
    if (!e) return;
    if (sub.startsWith("tier")) {
      const t = (e.tiers || [])[Number(sub.slice(4))];
      if (t) applyTo(t.unlock_when, v => t.unlock_when = v);
    } else if (sub.startsWith("act:")) {
      const a = ((e.actions && e.actions.add) || []).find(x => x.id === sub.slice(4));
      if (a) applyTo(a.unlock_when, v => a.unlock_when = v);
      // kind-default actions are shared config -- not editable from a placement's chip
    }
  } else {
    const tk = toKey.slice(0, 1), tid = toKey.slice(2);
    if (tk === "t") {
      const t = thoughts().find(x => x.id === tid);
      applyTo(t.unlock_when, v => t.unlock_when = v);
    } else if (tk === "e") {
      const e = encounters().find(x => x.id === tid);
      applyTo(e.visible_when, v => e.visible_when = v);
    } else if (tk === "f" && fk === "t") {
      thoughts().find(x => x.id === fid).set_flag = tid;
    } else {
      return;
    }
  }
  markDirty(); render();
}
