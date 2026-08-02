// Live validation, mirroring the load-time rules the game and map linter apply.
import { S, encounters, thoughts, remarks, actionsOf, allObservableIds, clauseList, obsOf,
         flagsOf } from "./state.js";

export function validate() {
  const out = [];
  const ids = new Set(), dup = new Set();
  allObservableIds().forEach(id => { if (ids.has(id)) dup.add(id); ids.add(id); });
  dup.forEach(id => out.push({ lvl: "err", msg: `duplicate id '${id}'` }));
  const setFlags = new Set();
  thoughts().forEach(t => t.set_flag && setFlags.add(t.set_flag));
  remarks().forEach(r => r.set_flag && setFlags.add(r.set_flag));
  encounters().forEach(e => actionsOf(e).forEach(a => a.set_flag && setFlags.add(a.set_flag)));
  S.scenes.forEach(s => {
    if (s.set_flag) setFlags.add(s.set_flag);
    (s.steps || []).forEach(st => st.set_flag && setFlags.add(st.set_flag));
  });
  const checkCond = (cond, where) => clauseList(cond).forEach(c => {
    obsOf(c).forEach(id => { if (!ids.has(id))
      out.push({ lvl: "err", msg: `${where}: unknown observed id '${id}'` }); });
    flagsOf(c).forEach(f => { if (!setFlags.has(f))
      out.push({ lvl: "warn", msg: `${where}: flag '${f}' is never set` }); });
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
  remarks().forEach(r => {
    if (!r.id) out.push({ lvl: "err", msg: "a remark has no id" });
    if (!r.text) out.push({ lvl: "warn", msg: `remark '${r.id}': no text` });
    if (r.voice && r.voice !== "player" && Object.keys(S.npcNames).length &&
        !S.npcNames[r.voice])
      out.push({ lvl: "warn", msg: `remark '${r.id}': voice '${r.voice}' has no config/npcs entry` });
    checkCond(r.unlock_when, `remark '${r.id}'`);
  });
  const encIds = new Set(encounters().map(e => e.id));
  const remarkIds = new Set(remarks().map(r => r.id));
  S.scenes.forEach(s => {
    if (!s.set_flag)
      out.push({ lvl: "err", msg: `scene '${s.id}': no set_flag (would replay forever; the game drops it)` });
    if (!(s.steps || []).length)
      out.push({ lvl: "err", msg: `scene '${s.id}': no steps` });
    checkCond(s.start_when, `scene '${s.id}' start_when`);
    (s.steps || []).forEach(st => {
      const enc = st.observe || st.menu;
      if (enc && !encIds.has(enc))
        out.push({ lvl: "err", msg: `scene '${s.id}': plays unknown encounter '${enc}'` });
      if (st.remark && !remarkIds.has(st.remark))
        out.push({ lvl: "err", msg: `scene '${s.id}': says unknown remark '${st.remark}'` });
    });
  });
  return out;
}
