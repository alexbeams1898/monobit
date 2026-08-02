// Graph derivation: nodes + edges rebuilt from the data every render; never stored.
//
// Node keys: "e:<id>" encounter, "t:<id>" thought, "s:<id>" scene, and CHIPS --
// "e:<id>#tier<i>" / "e:<id>#act:<aid>" -- the encounter's internals. An
// encounter is both a source (observing it yields knowledge) and a container
// (tiers to observe, deeds to take); chips give the contained things their own
// presence so an unlock edge lands on the thing it unlocks, not on the
// container's face -- otherwise "thought unlocks tier 2 of mom" would read as
// "thought precedes mom".
//
// FLAGS ARE EDGES, NOT NODES: a flag is a named wire from its setters to its
// readers, so each setter connects directly to each reader (the wire carries the
// flag's name). A set-but-never-read flag shows as a tag on its setter; scenes
// sharing a completion flag get a MUTEX tie (whichever runs first silences the
// other).
import { S, encounters, thoughts, remarks, actionsOf, allFlagNames, clauseList, obsOf,
         flagsOf }
  from "./state.js";

export const NODE_W = { enc: 190, thought: 190, remark: 190, chip: 166, scene: 190 };
export const NODE_H = { enc: 44, thought: 56, remark: 56, chip: 22, scene: 44 };
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
                   label: (a.say ? "💬 " : "") + (a.label || a.id), cond: a.unlock_when,
                   gated: gated(a.unlock_when), data: a });
    });
    chips.forEach((c, i) => c.chipIndex = i);
    nodes.push(...chips);
  });
  thoughts().forEach(t => nodes.push({ key: "t:" + t.id, kind: "thought", data: t }));
  remarks().forEach(r => nodes.push({ key: "r:" + r.id, kind: "remark", data: r }));
  S.scenes.forEach(s => nodes.push({ key: "s:" + s.id, kind: "scene", data: s }));

  // Where "observed <id>" knowledge COMES FROM. For an encounter that is its
  // base tier -- reaching tier 1 is what mints the observed key -- so unlock
  // edges originate at the tier-1 chip, not the identity card. (A fired thought
  // is its own memory; its node is the source.)
  const byObs = {};
  encounters().forEach(e =>
    byObs[e.id] = "e:" + e.id + ((e.tiers || []).length ? "#tier0" : ""));
  thoughts().forEach(t => byObs[t.id] = "t:" + t.id);
  remarks().forEach(r => byObs[r.id] = "r:" + r.id); // a said thing is a memory too

  // Every edge carries PROVENANCE: `info` names the exact field that relates the
  // two things, `remove` deletes that reference (clicking an edge shows both).
  // Flags never become edges directly here -- setters and readers are RECORDED,
  // then wired setter->reader after all passes.
  const edges = [];
  const flagSet = {}, flagRead = {};
  const setter = (f, key, why) => (flagSet[f] = flagSet[f] || []).push({ key, why });
  const reader = (f, key, why) => (flagRead[f] = flagRead[f] || []).push({ key, why });

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
      flagsOf(c).forEach(f => reader(f, toKey, where + suffix));
    });
  thoughts().forEach(t => {
    condEdges(t, "unlock_when", "t:" + t.id, "obs", `thought '${t.id}' unlock_when`);
    if (t.set_flag)
      setter(t.set_flag, "t:" + t.id, `thought '${t.id}' lands`);
  });
  remarks().forEach(r => {
    condEdges(r, "unlock_when", "r:" + r.id, "obs", `remark '${r.id}' unlock_when`);
    if (r.set_flag)
      setter(r.set_flag, "r:" + r.id, `remark '${r.id}' is said`);
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
        setter(a.set_flag, `${pkey}#act:${a.id}`, `deed '${a.id}' of '${e.id}' is taken`);
    };
    kindActs.forEach(a => actEdges(a, false));
    addActs.forEach(a => actEdges(a, true));
  });
  S.scenes.forEach(s => {
    const skey = "s:" + s.id;
    condEdges(s, "start_when", skey, "obs", `scene '${s.id}' start_when`, false);
    if (s.set_flag)
      setter(s.set_flag, skey, `scene '${s.id}' completes (its once-guard)`);
    (s.steps || []).forEach(st => {
      // The content id this step PLAYS -- an encounter (observe/menu) or a
      // remark; byObs maps both kinds of id to their node.
      const played = st.observe || st.menu || st.remark;
      if (played && byObs[played] && !byObs[played].startsWith("t:"))
        edges.push({ from: skey, to: parentOf(byObs[played]), cls: "plays",
          info: `scene '${s.id}' ${st.observe ? "observes" : st.remark ? "says" :
            "opens the menu of"} '${played}'` });
      if (st.set_flag)
        setter(st.set_flag, skey, `scene '${s.id}' reaches its set_flag step`);
    });
  });

  // The flag wires: each setter connects to each reader; the flag is the wire's
  // name, not a place of its own.
  const allFlags = new Set([...Object.keys(flagSet), ...Object.keys(flagRead)]);
  allFlags.forEach(f => {
    (flagSet[f] || []).forEach(s => (flagRead[f] || []).forEach(r =>
      edges.push({ from: s.key, to: r.key, cls: "flagwire", flag: f,
        info: `flag '${f}' -- set when ${s.why}; required in ${r.why}` })));
  });
  // Set-but-never-read flags: a tag on the setter, not a floating node.
  const unread = {};
  allFlags.forEach(f => {
    if (!(flagRead[f] || []).length)
      (flagSet[f] || []).forEach(s =>
        (unread[parentOf(s.key)] = unread[parentOf(s.key)] || []).push(f));
  });
  nodes.forEach(n => { if (unread[n.key]) n.setsUnread = unread[n.key]; });
  // Mutex ties: scenes sharing a completion flag are mutually exclusive.
  const byCompletion = {};
  S.scenes.forEach(s => { if (s.set_flag)
    (byCompletion[s.set_flag] = byCompletion[s.set_flag] || []).push(s.id); });
  Object.entries(byCompletion).forEach(([f, ids]) => {
    for (let i = 0; i < ids.length; ++i)
      for (let k = i + 1; k < ids.length; ++k)
        edges.push({ from: "s:" + ids[i], to: "s:" + ids[k], cls: "mutex",
          info: `'${ids[i]}' and '${ids[k]}' share completion flag '${f}' -- ` +
                `whichever runs first silences the other` });
  });

  const seen = new Set();
  return { nodes, edges: edges.filter(e => {
    const k = e.from + ">" + e.to + ":" + e.cls + (e.flag || "");
    if (seen.has(k) || (e.cls !== "mutex" && parentOf(e.from) === parentOf(e.to))) return false;
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
  // Depth = KNOWLEDGE flow only. Edges into chips (deepening returns) and PLAYS
  // edges (a scene firing content is choreography, not an unlock) never push a
  // node rightward -- otherwise encounters would sit "after" the scenes that
  // happen to voice them.
  const pEdges = graph.edges
    .filter(e => !e.to.includes("#") && e.cls !== "plays")
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
  // PROGRESSION rank: where in the WORLD a thing happens, from the map (levels
  // ranked by warp-distance from the start; placements pin encounters to levels;
  // scenes carry their own level; content a scene plays happens AT that scene's
  // place). Thoughts and flags inherit the earliest rank among their neighbors,
  // propagated to a fixpoint. Left -> right = the walk; knowledge depth then
  // orders WITHIN a place.
  const rankOfLevel = (lvl) => S.world.levels[lvl] ?? null;
  const prog = {};
  parents.forEach(n => {
    if (n.kind === "scene") prog[n.key] = rankOfLevel(n.data.level);
    else if (n.kind === "enc") prog[n.key] = rankOfLevel(S.world.placements[n.data.id]);
    else prog[n.key] = null;
  });
  S.scenes.forEach(s => (s.steps || []).forEach(st => {
    const enc = st.observe || st.menu;
    if (enc && prog["e:" + enc] == null)
      prog["e:" + enc] = rankOfLevel(s.level); // scene-fired content happens THERE
  }));
  // Pinned ranks (a real place) never move; everything else inherits the
  // EARLIEST rank among its neighbors, to a fixpoint.
  const pinned = new Set(Object.keys(prog).filter(k => prog[k] != null));
  for (let pass = 0; pass < parents.length; ++pass)
  {
    let changed = false;
    graph.edges.forEach(e => {
      const a = parentOf(e.from), b = parentOf(e.to);
      const inherit = (from, to) => {
        if (prog[from] == null || pinned.has(to))
          return;
        if (prog[to] == null || prog[from] < prog[to])
        {
          prog[to] = prog[from];
          changed = true;
        }
      };
      inherit(a, b);
      inherit(b, a);
    });
    if (!changed) break;
  }

  // Column = (place, then knowledge): sorted unique pairs become sequential
  // columns, so the walk orders the big sweeps and unlock depth orders inside.
  const colKey = (n) => (prog[n.key] ?? 0) * 1000 + Math.min(999, depth[n.key] || 0);
  const keys = [...new Set(parents.map(colKey))].sort((a, b) => a - b);
  const colIndex = {};
  keys.forEach((k, i) => colIndex[k] = i);
  parents.forEach(n => depth[n.key] = colIndex[colKey(n)]);

  const cols = {};
  parents.forEach(n => { (cols[depth[n.key]] = cols[depth[n.key]] || []).push(n); });

  // Neighbor map (ALL edges, chips collapsed) -- used to pull connected nodes to
  // nearby rows so links stay short instead of running the canvas' full height.
  const neighbors = {};
  graph.edges.forEach(e => {
    const a = parentOf(e.from), b = parentOf(e.to);
    (neighbors[a] = neighbors[a] || []).push(b);
    (neighbors[b] = neighbors[b] || []).push(a);
  });

  // Edges crossing each column gap: more traffic = a wider gap to carry it.
  const crossing = {};
  graph.edges.forEach(e => {
    const a = depth[parentOf(e.from)] ?? 0, b = depth[parentOf(e.to)] ?? 0;
    for (let d = Math.min(a, b); d < Math.max(a, b); ++d)
      crossing[d] = (crossing[d] || 0) + 1;
  });

  // Within a column: scenes on top (the director's lane), then content, flags at
  // the bottom -- and INSIDE each band, order by the average y of already-placed
  // neighbors (a barycenter pass, left to right) so connected things sit level.
  const rank = { scene: 0, enc: 1, thought: 2, remark: 2 };
  const maxDepth = Math.max(0, ...Object.keys(cols).map(Number));
  let x = 60;
  for (let d = 0; d <= maxDepth; ++d)
  {
    const ns = cols[d] || [];
    const bary = (n) => {
      const ys = (neighbors[n.key] || [])
        .map(k => S.layout[k] ? S.layout[k].y : null)
        .filter(v => v != null);
      return ys.length ? ys.reduce((s, v) => s + v, 0) / ys.length : 1e9;
    };
    ns.sort((a, b) => (rank[a.kind] ?? 9) - (rank[b.kind] ?? 9) || bary(a) - bary(b));
    let y = 60;
    ns.forEach(n => {
      S.layout[n.key] = { x, y };
      y += nodeExtent(graph, n).h + 34;
    });
    x += NODE_W.enc + Math.min(420, 90 + (crossing[d] || 0) * 9);
  }
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
