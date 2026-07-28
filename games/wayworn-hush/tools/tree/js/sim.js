// Simulation: the knowledge fixpoint, in miniature. The user toggles primitives
// (observed encounters, landed thoughts, flags, stat levels); availability of
// everything else re-derives on each change -- nothing is stored.
import { S, thoughts, clauseList, obsOf, flagsOf } from "./state.js";

export function simKnowledge() {
  const known = new Set([...S.sim.observed, ...S.sim.fired]);
  const flags = new Set(S.sim.flags);
  thoughts().forEach(t => { if (S.sim.fired.has(t.id) && t.set_flag) flags.add(t.set_flag); });
  return { known, flags };
}

export function condMet(cond, k) {
  const clauses = clauseList(cond);
  if (!clauses.length) return true;
  return clauses.some(c =>
    obsOf(c).every(id => k.known.has(id)) &&
    flagsOf(c).every(f => k.flags.has(f)) &&
    Object.entries(c.stat || {}).every(([s, lvl]) => (S.sim.stats[s] || 0) >= lvl));
}

export function simToggle(key) {
  const [kind, id] = [key.slice(0, 1), key.slice(2)];
  const flip = (set, v) => set.has(v) ? set.delete(v) : set.add(v);
  if (kind === "e") flip(S.sim.observed, id);
  else if (kind === "t" || kind === "r") flip(S.sim.fired, id);
  else if (kind === "s") {
    // Toggling a scene = pretending it completed: its flag flips, and everything
    // sequenced on it lights up (including silencing its exclusivity siblings).
    const sc = S.scenes.find(x => x.id === id);
    if (sc && sc.set_flag) flip(S.sim.flags, sc.set_flag);
  }
  // Bare flags are toggled from the sim panel's checklist (they have no nodes).
}

export function simReset() {
  S.sim = { observed: new Set(), fired: new Set(), flags: new Set(), stats: S.sim.stats };
}
