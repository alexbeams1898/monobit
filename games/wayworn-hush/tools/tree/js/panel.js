// The side-panel editors: structured forms over the raw objects; anything a form
// doesn't cover is preserved untouched and listed so nothing silently vanishes.
import { S, esc, encounters, thoughts, remarks, actionsOf, allObservableIds, clauseList,
         obsOf, flagsOf, markDirty, renameObservable } from "./state.js";
import { edgeKey } from "./render.js";

const $ = (id) => document.getElementById(id);
let rerender = () => {};
export function initPanel(renderFn) { rerender = renderFn; }

const KNOWN = {
  enc: ["id", "kind", "value", "speaker", "trigger", "visible_when", "tiers", "actions",
        "_comment", "x", "y", "w", "h"],
  thought: ["id", "faculty", "text", "miss_text", "set_flag", "emotional_weight",
            "feeders", "unlock_when", "_comment"],
  action: ["id", "label", "say", "result_text", "set_flag", "one_shot", "consumes_spot",
           "unlock_when", "_comment"],
};

export function closePanel() {
  S.selected = null;
  S.selectedEdge = null;
  $("panel").className = "";
}

// The edge panel: what relates the two ends -- the exact field the edge derives
// from -- plus the option to sever exactly that reference.
export function openEdgePanel(edge) {
  S.selected = null;
  S.selectedEdge = edgeKey(edge);
  rerender();
  const p = $("panel");
  p.className = "open";
  p.innerHTML = "";
  const h = document.createElement("h2"); h.textContent = "Connection"; p.appendChild(h);
  const kindDesc = {
    obs: "unlocks-from: the target requires the source as held knowledge",
    flagwire: "a flag wire: the source's act raises the flag; the target requires it",
    visible: "visibility: the target encounter is hidden until this holds",
    plays: "choreography: the scene fires this content",
    mutex: "mutual exclusion: shared completion flag -- only one can ever run",
  };
  const d = document.createElement("div");
  d.innerHTML = `<div class="dimtext" style="margin-bottom:6px">${esc(kindDesc[edge.cls] || edge.cls)}</div>
    <div>${esc(edge.info)}</div>`;
  p.appendChild(d);
  if (edge.remove) {
    const b = document.createElement("button");
    b.className = "danger";
    b.textContent = "Remove this reference";
    b.onclick = () => { edge.remove(); closePanel(); markDirty(); rerender(); };
    p.appendChild(b);
  }
}

export function openPanel(key) {
  S.selected = key.includes("#") ? key.split("#")[0] : key; // a chip selects its parent
  S.selectedEdge = null;
  const p = $("panel");
  p.className = "open";
  p.innerHTML = "";
  const kind = S.selected.slice(0, 1), id = S.selected.slice(2);
  if (kind === "t") return openThoughtPanel(p, thoughts().find(t => t.id === id));
  if (kind === "r") return openRemarkPanel(p, remarks().find(r => r.id === id));
  if (kind === "s") return openScenePanel(p, S.scenes.find(s => s.id === id));
  return openEncounterPanel(p, encounters().find(e => e.id === id));
}

// A remark: said, not written -- the spoken sibling of a thought.
function openRemarkPanel(p, r) {
  if (!r) return;
  const h = document.createElement("h2"); h.textContent = "Remark"; p.appendChild(h);
  field(p, "id", r.id, v => { renameObservable(r.id, v); r.id = v; });
  field(p, "voice (player, or an npc id)", r.voice || "player",
        v => r.voice = (v && v !== "player") ? v : undefined);
  field(p, "text (the words said)", r.text, v => r.text = v, "textarea");
  field(p, "miss_text (eligible but the roll missed -- stays inner)", r.miss_text,
        v => r.miss_text = v || undefined, "textarea");
  field(p, "set_flag", r.set_flag, v => r.set_flag = v || undefined);
  heading(p, "said when");
  condEditor(p, r.unlock_when, v => { r.unlock_when = v; markDirty(); rerender(); });
  extraNote(p, r, ["id", "voice", "text", "miss_text", "set_flag", "unlock_when", "_comment"]);
  deleteButton(p, "remark", () => {
    S.doc.remarks.splice(S.doc.remarks.indexOf(r), 1);
  });
}

// Scenes are read-only in the tool (hand-edited files): show the choreography and
// where to edit it.
function openScenePanel(p, s) {
  if (!s) return;
  const h = document.createElement("h2"); h.textContent = "Scene: " + s.id; p.appendChild(h);
  const d = document.createElement("div");
  const stepLine = (st) => {
    if (st.enter) return `enter ${st.enter} at ${st.at || "?"}`;
    if (st.leave) return `leave ${st.leave}`;
    if (st.move) return `move ${st.move} → ${st.to || "?"}`;
    if (st.face) return `face ${st.face} ${st.dir || ""}`;
    if (st.wait != null) return `wait ${st.wait}s`;
    if (st.observe) return `observe ${st.observe}`;
    if (st.menu) return `menu ${st.menu}`;
    if (st.set_flag) return `set_flag ${st.set_flag}`;
    if (st.sound) return `sound ${st.sound}`;
    if (st.stop_sound) return `stop_sound ${st.stop_sound}`;
    return "?";
  };
  d.innerHTML = `<div>level: <b>${esc(s.level || "?")}</b> · completes → ⚑ ${esc(s.set_flag || "MISSING")}</div>
    <ol style="margin:8px 0 0 18px">${(s.steps || []).map(st =>
      `<li>${esc(stepLine(st))}</li>`).join("")}</ol>
    <div class="dimtext" style="margin-top:8px">read-only here — edit
    config/scenes/${esc(s.id)}.json (Reload picks it up)</div>`;
  p.appendChild(d);
}

function field(holder, label, value, onChange, kind = "text") {
  const l = document.createElement("label"); l.textContent = label;
  const i = document.createElement(kind === "textarea" ? "textarea" : "input");
  if (kind !== "textarea") i.type = kind;
  i.value = value == null ? "" : value;
  i.onchange = () => { onChange(kind === "number" ? Number(i.value) : i.value); markDirty(); rerender(); };
  holder.appendChild(l); holder.appendChild(i);
  return i;
}
function checkbox(holder, label, value, onChange) {
  const l = document.createElement("label");
  l.innerHTML = `<input type="checkbox" ${value ? "checked" : ""}> ${esc(label)}`;
  l.querySelector("input").onchange = (ev) => { onChange(ev.target.checked); markDirty(); rerender(); };
  holder.appendChild(l);
}
function heading(holder, text) {
  const h = document.createElement("h3"); h.textContent = text; holder.appendChild(h);
}
function extraNote(holder, obj, known) {
  const extra = Object.keys(obj).filter(k => !known.includes(k));
  if (!extra.length) return;
  const d = document.createElement("div");
  d.className = "dimtext";
  d.style.marginTop = "6px";
  d.textContent = "preserved fields (edit in file): " + extra.join(", ");
  holder.appendChild(d);
}
function deleteButton(p, what, doDelete) {
  const b = document.createElement("button");
  b.className = "danger";
  b.textContent = "Delete " + what;
  b.onclick = () => {
    if (!confirm(`Delete this ${what}? References to it will show as validation errors.`)) return;
    doDelete(); closePanel(); markDirty(); rerender();
  };
  p.appendChild(b);
}

// Structured condition editor over the clause array (clauses OR; fields AND).
export function condEditor(holder, cond, onChange, allowStat = true) {
  const wrap = document.createElement("div");
  const clauses = clauseList(cond).map(c => JSON.parse(JSON.stringify(c)));
  const commit = () => {
    const cleaned = clauses.filter(c =>
      obsOf(c).length || flagsOf(c).length || Object.keys(c.stat || {}).length);
    onChange(cleaned.length ? cleaned : undefined);
  };
  // Authored form stays minimal: one flag saves as a string, several as an array.
  const setFlags = (c, list) =>
    c.flag = list.length === 0 ? undefined : (list.length === 1 ? list[0] : list);
  const redraw = () => {
    wrap.innerHTML = "";
    clauses.forEach((c, ci) => {
      const div = document.createElement("div");
      div.className = "clause";
      const obsChips = obsOf(c).map((id, i) =>
        `<span class="chip">${esc(id)}<span class="x" data-obs="${i}">×</span></span>`).join("");
      const flagChips = flagsOf(c).map((f, i) =>
        `<span class="chip">${esc(f)}<span class="x" data-flagdel="${i}">×</span></span>`).join("");
      const statChips = Object.entries(c.stat || {}).map(([s, lvl]) =>
        `<span class="chip">${esc(s)} ≥ ${lvl}<span class="x" data-stat="${esc(s)}">×</span></span>`).join("");
      div.innerHTML = `
        <div>observed: ${obsChips}
          <select data-addobs><option value="">+ add…</option>
          ${allObservableIds().filter(id => !obsOf(c).includes(id)).map(id =>
            `<option>${esc(id)}</option>`).join("")}</select></div>
        <div style="margin-top:5px">flags: ${flagChips}
          <input type="text" data-flag placeholder="+ flag…" style="width:130px"
            list="flagNames"></div>
        ${allowStat ? `<div style="margin-top:5px">stat gates: ${statChips}
          <span class="row" style="margin-top:3px">
            <input type="text" data-statname placeholder="stat" style="flex:2">
            <input type="number" data-statlvl placeholder="lvl" style="flex:1">
            <button class="small" data-addstat>+</button></span></div>` : ""}
        <div style="text-align:right"><button class="del" data-delclause>remove clause</button></div>
        ${ci < clauses.length - 1 ? '<div class="or">— OR —</div>' : ""}`;
      div.querySelectorAll(".x[data-obs]").forEach(x => x.onclick = () => {
        const list = obsOf(c); list.splice(Number(x.dataset.obs), 1);
        c.observed = list.length ? list : undefined; commit(); redraw();
      });
      div.querySelector("[data-addobs]").onchange = (ev) => {
        if (!ev.target.value) return;
        c.observed = obsOf(c).concat(ev.target.value); commit(); redraw();
      };
      div.querySelector("[data-flag]").onchange = (ev) => {
        const f = ev.target.value.trim();
        if (!f || flagsOf(c).includes(f)) return;
        setFlags(c, flagsOf(c).concat(f)); commit(); redraw();
      };
      div.querySelectorAll(".x[data-flagdel]").forEach(x => x.onclick = () => {
        const list = flagsOf(c); list.splice(Number(x.dataset.flagdel), 1);
        setFlags(c, list); commit(); redraw();
      });
      if (allowStat) {
        div.querySelector("[data-addstat]").onclick = () => {
          const s = div.querySelector("[data-statname]").value.trim();
          const lvl = Number(div.querySelector("[data-statlvl]").value);
          if (!s || !lvl) return;
          c.stat = c.stat || {}; c.stat[s] = lvl; commit(); redraw();
        };
        div.querySelectorAll(".x[data-stat]").forEach(x => x.onclick = () => {
          delete c.stat[x.dataset.stat]; commit(); redraw();
        });
      }
      div.querySelector("[data-delclause]").onclick = () => {
        clauses.splice(ci, 1); commit(); redraw();
      };
      wrap.appendChild(div);
    });
    const add = document.createElement("button");
    add.className = "small"; add.style.marginTop = "6px";
    add.textContent = "+ OR clause";
    add.onclick = () => { clauses.push({}); redraw(); };
    wrap.appendChild(add);
  };
  redraw();
  holder.appendChild(wrap);
}

function openEncounterPanel(p, e) {
  if (!e) return;
  const h = document.createElement("h2");
  h.textContent = "Encounter";
  p.appendChild(h);
  field(p, "id", e.id, v => { renameObservable(e.id, v); e.id = v; });
  field(p, "kind (action defaults from actions.json)", e.kind, v => e.kind = v || undefined);
  field(p, "value (authored worth)", e.value == null ? 1 : e.value, v => e.value = v, "number");
  field(p, "speaker (npc id; empty = a thing, set = a person)", e.speaker,
        v => e.speaker = v || undefined);
  const trig = document.createElement("label"); trig.textContent = "trigger";
  const sel = document.createElement("select");
  ["Observe", "Enter"].forEach(t => {
    const o = document.createElement("option"); o.textContent = t;
    o.selected = (e.trigger || "Observe") === t; sel.appendChild(o);
  });
  sel.onchange = () => { e.trigger = sel.value === "Observe" ? undefined : sel.value; markDirty(); rerender(); };
  p.appendChild(trig); p.appendChild(sel);

  heading(p, "visible when");
  condEditor(p, e.visible_when, v => { e.visible_when = v; markDirty(); rerender(); });

  heading(p, "tiers (observe depth -- the player's perception)");
  (e.tiers || []).forEach((t, i) => {
    const s = document.createElement("div"); s.className = "sub";
    s.innerHTML = `<div class="head"><b>tier ${i + 1}</b>
      <button class="del">delete</button></div>`;
    s.querySelector(".del").onclick = () => { e.tiers.splice(i, 1); markDirty(); rerender(); openPanel(S.selected); };
    field(s, "text", t.text, v => t.text = v, "textarea");
    const lbl = document.createElement("label"); lbl.textContent = "unlock when"; s.appendChild(lbl);
    condEditor(s, t.unlock_when, v => { t.unlock_when = v; markDirty(); rerender(); });
    extraNote(s, t, ["text", "unlock_when", "_comment"]);
    p.appendChild(s);
  });
  const addT = document.createElement("button"); addT.className = "small";
  addT.style.marginTop = "6px"; addT.textContent = "+ tier";
  addT.onclick = () => { (e.tiers = e.tiers || []).push({ text: "" }); markDirty(); rerender(); openPanel(S.selected); };
  p.appendChild(addT);

  heading(p, "actions" + (e.speaker ? " (their replies = result_text)" : ""));
  const kindActs = (S.actionKinds[e.kind] && S.actionKinds[e.kind].actions) || [];
  if (kindActs.length) {
    const d = document.createElement("div");
    d.className = "dimtext";
    d.textContent = `from kind '${e.kind}': ` + kindActs.map(a => a.id).join(", ");
    p.appendChild(d);
  }
  ((e.actions && e.actions.add) || []).forEach((a, i) => {
    const s = document.createElement("div"); s.className = "sub";
    s.innerHTML = `<div class="head"><b>${esc(a.id || "action")}</b>
      <button class="del">delete</button></div>`;
    s.querySelector(".del").onclick = () => { e.actions.add.splice(i, 1); markDirty(); rerender(); openPanel(S.selected); };
    field(s, "id", a.id, v => a.id = v);
    field(s, "label (the menu line)", a.label, v => a.label = v);
    field(s, "say (the player's spoken words, quoted under his name)", a.say,
          v => a.say = v || undefined, "textarea");
    field(s, "result_text" + (e.speaker ? " (spoken by them)" : ""), a.result_text,
          v => a.result_text = v || undefined, "textarea");
    field(s, "set_flag", a.set_flag, v => a.set_flag = v || undefined);
    checkbox(s, "one_shot", a.one_shot, v => a.one_shot = v || undefined);
    checkbox(s, "consumes_spot (removes the spot; ends its menu)", a.consumes_spot,
             v => a.consumes_spot = v || undefined);
    const lbl = document.createElement("label"); lbl.textContent = "offered when"; s.appendChild(lbl);
    condEditor(s, a.unlock_when, v => { a.unlock_when = v; markDirty(); rerender(); });
    extraNote(s, a, KNOWN.action);
    p.appendChild(s);
  });
  const addA = document.createElement("button"); addA.className = "small";
  addA.style.marginTop = "6px"; addA.textContent = "+ action";
  addA.onclick = () => {
    e.actions = e.actions || {}; e.actions.add = e.actions.add || [];
    e.actions.add.push({ id: "new_action", label: "" });
    markDirty(); rerender(); openPanel(S.selected);
  };
  p.appendChild(addA);
  extraNote(p, e, KNOWN.enc);
  deleteButton(p, "encounter", () => {
    S.doc.encounters.splice(S.doc.encounters.indexOf(e), 1);
  });
}

function openThoughtPanel(p, t) {
  if (!t) return;
  const h = document.createElement("h2"); h.textContent = "Thought"; p.appendChild(h);
  field(p, "id", t.id, v => { renameObservable(t.id, v); t.id = v; });
  field(p, "faculty (perception / reason / wonder)", t.faculty, v => t.faculty = v || undefined);
  field(p, "text (the thought, the player's voice)", t.text, v => t.text = v, "textarea");
  field(p, "miss_text (eligible but the roll missed)", t.miss_text,
        v => t.miss_text = v || undefined, "textarea");
  checkbox(p, "spoken (said aloud in the player's voice; never written to the notebook)",
           t.spoken, v => t.spoken = v || undefined);
  field(p, "set_flag", t.set_flag, v => t.set_flag = v || undefined);
  field(p, "emotional_weight", t.emotional_weight || 0, v => t.emotional_weight = v || undefined, "number");
  heading(p, "unlocks when");
  condEditor(p, t.unlock_when, v => { t.unlock_when = v; markDirty(); rerender(); });
  extraNote(p, t, KNOWN.thought);
  deleteButton(p, "thought", () => {
    S.doc.thoughts.splice(S.doc.thoughts.indexOf(t), 1);
  });
}

