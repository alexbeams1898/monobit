// Live validation, mirroring the load-time rules the game and map linter apply.
import { S, encounters, thoughts, actionsOf, allObservableIds, clauseList, obsOf }
  from "./state.js";

export function validate() {
  const out = [];
  const ids = new Set(), dup = new Set();
  allObservableIds().forEach(id => { if (ids.has(id)) dup.add(id); ids.add(id); });
  dup.forEach(id => out.push({ lvl: "err", msg: `duplicate id '${id}'` }));
  const setFlags = new Set();
  thoughts().forEach(t => t.set_flag && setFlags.add(t.set_flag));
  encounters().forEach(e => actionsOf(e).forEach(a => a.set_flag && setFlags.add(a.set_flag)));
  const checkCond = (cond, where) => clauseList(cond).forEach(c => {
    obsOf(c).forEach(id => { if (!ids.has(id))
      out.push({ lvl: "err", msg: `${where}: unknown observed id '${id}'` }); });
    if (c.flag && !setFlags.has(c.flag))
      out.push({ lvl: "warn", msg: `${where}: flag '${c.flag}' is never set` });
  });
  encounters().forEach(e => {
    if (!e.id) out.push({ lvl: "err", msg: "an encounter has no id" });
    if (!(e.tiers || []).length)
      out.push({ lvl: "err", msg: `encounter '${e.id}': no tiers (nothing to observe)` });
    if (!actionsOf(e).length)
      out.push({ lvl: "err", msg: `encounter '${e.id}': no actions (nothing to do) -- add one or set a kind` });
    if (e.speaker && Object.keys(S.npcNames).length && !S.npcNames[e.speaker])
      out.push({ lvl: "warn", msg: `encounter '${e.id}': speaker '${e.speaker}' has no config/npcs entry` });
    checkCond(e.visible_when, `encounter '${e.id}' visible_when`);
    (e.tiers || []).forEach((t, i) => checkCond(t.unlock_when, `'${e.id}' tier ${i + 1}`));
    ((e.actions && e.actions.add) || []).forEach(a =>
      checkCond(a.unlock_when, `'${e.id}' action '${a.id}'`));
  });
  thoughts().forEach(t => {
    if (!t.id) out.push({ lvl: "err", msg: "a thought has no id" });
    if (!t.text) out.push({ lvl: "warn", msg: `thought '${t.id}': no text` });
    checkCond(t.unlock_when, `thought '${t.id}'`);
  });
  return out;
}
