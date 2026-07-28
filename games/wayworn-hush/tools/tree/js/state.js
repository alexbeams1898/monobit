// Shared state + data helpers. The doc is the parsed psyche.json, mutated
// in place so unknown fields round-trip untouched.
export const S = {
  doc: null,          // psyche.json
  layout: {},         // node key -> {x, y} (the committed sidecar)
  actionKinds: {},    // kind -> {actions: [...]} (read-only context)
  npcNames: {},       // npc id -> display name (read-only context)
  scenes: [],         // config/scenes/*.json (read-only in the tool; hand-edited)
  world: { levels: {}, placements: {} }, // the map's progression: level ranks + encounter homes
  dirty: false,
  connected: false,
  selected: null,     // node key
  simOn: false,
  sim: { observed: new Set(), fired: new Set(), flags: new Set(), stats: {} },
  view: { x: 40, y: 40, k: 1 },
  onChange: () => {}, // re-render hook, set by main
};

export const encounters = () => (S.doc && S.doc.encounters) || [];
export const thoughts = () => (S.doc && S.doc.thoughts) || [];
export const remarks = () => (S.doc && S.doc.remarks) || []; // said, not written
export const clauseList = (cond) => Array.isArray(cond) ? cond : [];
export const obsOf = (clause) => {
  const o = clause.observed;
  return o == null ? [] : (Array.isArray(o) ? o : [o]);
};
// A clause's flag field, like observed, is a string or an array (ALL required).
export const flagsOf = (clause) => {
  const f = clause.flag;
  return f == null || f === "" ? [] : (Array.isArray(f) ? f : [f]);
};
// Every action an encounter offers: kind defaults + its own add list.
export function actionsOf(enc) {
  const kind = (S.actionKinds[enc.kind] && S.actionKinds[enc.kind].actions) || [];
  const add = (enc.actions && enc.actions.add) || [];
  return kind.concat(add);
}
export function allFlagNames() {
  const flags = new Set();
  const scan = (cond) => clauseList(cond).forEach(c => flagsOf(c).forEach(f => flags.add(f)));
  thoughts().forEach(t => { if (t.set_flag) flags.add(t.set_flag); scan(t.unlock_when); });
  encounters().forEach(e => {
    scan(e.visible_when);
    (e.tiers || []).forEach(t => scan(t.unlock_when));
    actionsOf(e).forEach(a => { if (a.set_flag) flags.add(a.set_flag); scan(a.unlock_when); });
  });
  S.scenes.forEach(s => {
    if (s.set_flag) flags.add(s.set_flag);
    scan(s.start_when);
    (s.steps || []).forEach(st => { if (st.set_flag) flags.add(st.set_flag); });
  });
  remarks().forEach(r => { if (r.set_flag) flags.add(r.set_flag); scan(r.unlock_when); });
  return [...flags];
}
export function allStatNames() {
  const stats = new Set();
  const scan = (cond) => clauseList(cond).forEach(c =>
    Object.keys(c.stat || {}).forEach(s => stats.add(s)));
  thoughts().forEach(t => scan(t.unlock_when));
  encounters().forEach(e => {
    scan(e.visible_when);
    (e.tiers || []).forEach(t => scan(t.unlock_when));
    actionsOf(e).forEach(a => scan(a.unlock_when));
  });
  return [...stats];
}
export const allObservableIds = () =>
  encounters().map(e => e.id)
    .concat(thoughts().map(t => t.id))
    .concat(remarks().map(r => r.id));

export function uniqueId(base) {
  let id = base, n = 2;
  const ids = new Set(allObservableIds());
  while (ids.has(id)) id = base + "_" + n++;
  return id;
}
export function markDirty() { S.dirty = true; }

// Renaming an observable updates every reference to it.
export function renameObservable(oldId, newId) {
  if (!oldId || oldId === newId) return;
  const fix = (cond) => clauseList(cond).forEach(c => {
    if (Array.isArray(c.observed)) c.observed = c.observed.map(x => x === oldId ? newId : x);
    else if (c.observed === oldId) c.observed = newId;
  });
  thoughts().forEach(t => fix(t.unlock_when));
  remarks().forEach(r => fix(r.unlock_when));
  encounters().forEach(e => {
    fix(e.visible_when);
    (e.tiers || []).forEach(t => fix(t.unlock_when));
    ((e.actions && e.actions.add) || []).forEach(a => fix(a.unlock_when));
  });
  ["e:", "t:", "r:"].forEach(p => {
    if (S.layout[p + oldId]) { S.layout[p + newId] = S.layout[p + oldId]; delete S.layout[p + oldId]; }
  });
  markDirty();
}

export function esc(s) { return String(s).replace(/&/g, "&amp;").replace(/</g, "&lt;"); }
export function trunc(s, n) { s = String(s || ""); return s.length > n ? s.slice(0, n - 1) + "…" : s; }
