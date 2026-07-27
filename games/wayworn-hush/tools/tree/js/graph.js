// Graph derivation: nodes + edges rebuilt from the data every render; never stored.
//
// Node keys: "e:<id>" encounter, "t:<id>" thought, "f:<name>" flag, and CHIPS --
// "e:<id>#tier<i>" / "e:<id>#act:<aid>" -- the encounter's internals. An
// encounter is both a source (observing it yields knowledge) and a container
// (tiers to observe, deeds to take); chips give the contained things their own
// presence so an unlock edge lands on the thing it unlocks, not on the
// container's face -- otherwise "thought unlocks tier 2 of mom" would read as
// "thought precedes mom".
import { S, encounters, thoughts, actionsOf, allFlagNames, clauseList, obsOf } from "./state.js";

export const NODE_W = { enc: 190, thought: 190, flag: 150, chip: 166 };
export const NODE_H = { enc: 44, thought: 56, flag: 30, chip: 22 };
export const CHIP_GAP = 4;

export const parentOf = (key) => key.includes("#") ? key.split("#")[0] : key;

export function buildGraph() {
  const nodes = [];
  const gated = (cond) => clauseList(cond).length > 0;

  // The parent card is IDENTITY only; every tier (observe surface) and every deed
  // (act surface) is a chip, grouped tiers-then-deeds, so the encounter's whole
  // shape is visible and edges land on the exact thing they gate.
  encounters().forEach(e => {
    const pkey = "e:" + e.id;
    nodes.push({ key: pkey, kind: "enc", data: e });
    const chips = [];
    (e.tiers || []).forEach((t, i) => {
      chips.push({ key: `${pkey}#tier${i}`, kind: "chip", sub: "tier", parent: pkey,
                   label: `${i + 1} · ${t.text || "(empty)"}`, cond: t.unlock_when,
                   gated: gated(t.unlock_when), data: t });
    });
    const kindActs = (S.actionKinds[e.kind] && S.actionKinds[e.kind].actions) || [];
    const addActs = (e.actions && e.actions.add) || [];
    kindActs.concat(addActs).forEach(a => {
      chips.push({ key: `${pkey}#act:${a.id}`, kind: "chip", sub: "act", parent: pkey,
                   label: a.label || a.id, cond: a.unlock_when,
                   gated: gated(a.unlock_when), data: a });
    });
    chips.forEach((c, i) => c.chipIndex = i);
    nodes.push(...chips);
  });
  thoughts().forEach(t => nodes.push({ key: "t:" + t.id, kind: "thought", data: t }));
  allFlagNames().forEach(f => nodes.push({ key: "f:" + f, kind: "flag", name: f }));

  // Where "observed <id>" knowledge COMES FROM. For an encounter that is its
  // base tier -- reaching tier 1 is what mints the observed key -- so unlock
  // edges originate at the tier-1 chip, not the identity card. (A fired thought
  // is its own memory; its node is the source.)
  const byObs = {};
  encounters().forEach(e =>
    byObs[e.id] = "e:" + e.id + ((e.tiers || []).length ? "#tier0" : ""));
  thoughts().forEach(t => byObs[t.id] = "t:" + t.id);

  // Every edge carries PROVENANCE: `info` names the exact field that relates the
  // two things, `remove` deletes that reference (clicking an edge shows both).
  const edges = [];
  const condEdges = (holder, condKey, toKey, cls, where, editable = true) =>
    clauseList(holder[condKey]).forEach((c, ci) => {
      const suffix = (clauseList(holder[condKey]).length > 1 ? `, clause ${ci + 1}` : "") +
                     (editable ? "" : " (a kind default -- edit actions.json)");
      obsOf(c).forEach(id => { if (byObs[id]) edges.push({
        from: byObs[id], to: toKey, cls,
        info: `'${id}' is required (observed) in ${where}` + suffix,
        remove: editable ? () => {
          const list = obsOf(c).filter(x => x !== id);
          c.observed = list.length ? list : undefined;
        } : undefined }); });
      if (c.flag) edges.push({
        from: "f:" + c.flag, to: toKey, cls: "gateflag",
        info: `flag '${c.flag}' is required in ${where}` + suffix,
        remove: editable ? () => { c.flag = undefined; } : undefined });
    });
  thoughts().forEach(t => {
    condEdges(t, "unlock_when", "t:" + t.id, "obs", `thought '${t.id}' unlock_when`);
    if (t.set_flag) edges.push({
      from: "t:" + t.id, to: "f:" + t.set_flag, cls: "setflag",
      info: `thought '${t.id}' sets flag '${t.set_flag}' when it lands`,
      remove: () => { t.set_flag = undefined; } });
  });
  encounters().forEach(e => {
    const pkey = "e:" + e.id;
    condEdges(e, "visible_when", pkey, "visible", `encounter '${e.id}' visible_when`);
    (e.tiers || []).forEach((t, i) => {
      condEdges(t, "unlock_when", `${pkey}#tier${i}`, "obs",
                `'${e.id}' tier ${i + 1} unlock_when`);
    });
    const kindActs = (S.actionKinds[e.kind] && S.actionKinds[e.kind].actions) || [];
    const addActs = (e.actions && e.actions.add) || [];
    const actEdges = (a, editable) => {
      condEdges(a, "unlock_when", `${pkey}#act:${a.id}`, "obs",
                `'${e.id}' action '${a.id}' offered-when`, editable);
      if (a.set_flag)
        edges.push({ from: `${pkey}#act:${a.id}`, to: "f:" + a.set_flag, cls: "setflag",
          info: `'${e.id}' action '${a.id}' sets flag '${a.set_flag}' when taken` +
                (editable ? "" : " (a kind default -- edit actions.json)"),
          remove: editable ? () => { a.set_flag = undefined; } : undefined });
    };
    kindActs.forEach(a => actEdges(a, false));
    addActs.forEach(a => actEdges(a, true));
  });

  const seen = new Set();
  return { nodes, edges: edges.filter(e => {
    const k = e.from + ">" + e.to + ":" + e.cls;
    if (seen.has(k) || parentOf(e.from) === parentOf(e.to)) return false;
    seen.add(k); return true;
  }) };
}

// Position of any node. Parents live in the layout; chips hang below their parent.
export function posOf(key) {
  if (!key.includes("#"))
    return S.layout[key] || (S.layout[key] = { x: 60, y: 60 });
  return null; // chips are positioned via chipPos with their graph entry
}
export function chipPos(chip) {
  const p = posOf(chip.parent);
  return { x: p.x + (NODE_W.enc - NODE_W.chip),
           y: p.y + NODE_H.enc + CHIP_GAP + chip.chipIndex * (NODE_H.chip + CHIP_GAP) };
}
export function nodePos(graph, key) {
  const n = graph.nodes.find(x => x.key === key);
  return n && n.kind === "chip" ? chipPos(n) : posOf(key);
}
// A parent's footprint including its chip stack (for layout + fit).
export function nodeExtent(graph, n) {
  const pos = n.kind === "chip" ? chipPos(n) : posOf(n.key);
  let h = NODE_H[n.kind];
  if (n.kind === "enc") {
    const chips = graph.nodes.filter(c => c.kind === "chip" && c.parent === n.key).length;
    h += chips * (NODE_H.chip + CHIP_GAP);
  }
  return { x: pos.x, y: pos.y, w: NODE_W[n.kind], h };
}

// Auto-layout: columns by longest dependency depth. Edges INTO chips are
// excluded from depth -- a thought deepening an earlier encounter is a RETURN
// into it (a back-edge by nature), not a dependency of the encounter itself;
// counting it would recreate the parent-level cycle chips exist to dissolve.
// Chip SOURCES still collapse to the parent (what a deed sets flows onward).
export function autoLayout(graph) {
  const parents = graph.nodes.filter(n => n.kind !== "chip");
  const depth = {};
  parents.forEach(n => depth[n.key] = 0);
  const pEdges = graph.edges
    .filter(e => !e.to.includes("#"))
    .map(e => ({ from: parentOf(e.from), to: e.to }))
    .filter(e => e.from !== e.to);
  for (let pass = 0; pass < parents.length; ++pass) {
    let changed = false;
    pEdges.forEach(e => {
      if (depth[e.from] != null && depth[e.to] != null && depth[e.to] < depth[e.from] + 1) {
        depth[e.to] = depth[e.from] + 1; changed = true;
      }
    });
    if (!changed) break;
  }
  const cols = {};
  parents.forEach(n => { (cols[depth[n.key]] = cols[depth[n.key]] || []).push(n); });
  Object.entries(cols).forEach(([d, ns]) => {
    let y = 60;
    ns.forEach(n => {
      S.layout[n.key] = { x: 60 + d * 280, y };
      y += nodeExtent(graph, n).h + 34;
    });
  });
}

// Fit the whole tree in the viewport, centered.
export function fitView(graph, vw, vh) {
  const parents = graph.nodes.filter(n => n.kind !== "chip");
  if (!parents.length) return;
  let x0 = Infinity, y0 = Infinity, x1 = -Infinity, y1 = -Infinity;
  parents.forEach(n => {
    const e = nodeExtent(graph, n);
    x0 = Math.min(x0, e.x); y0 = Math.min(y0, e.y);
    x1 = Math.max(x1, e.x + e.w); y1 = Math.max(y1, e.y + e.h);
  });
  S.view.k = Math.min(1.25, (vw - 80) / Math.max(1, x1 - x0), (vh - 80) / Math.max(1, y1 - y0));
  S.view.x = (vw - (x1 - x0) * S.view.k) / 2 - x0 * S.view.k;
  S.view.y = (vh - (y1 - y0) * S.view.k) / 2 - y0 * S.view.k;
}
