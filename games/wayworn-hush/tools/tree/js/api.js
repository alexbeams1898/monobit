// Talks to tree.py, which roots itself at the game folder it lives in -- the
// tool always edits the game it ships inside; nothing to pick.
import { S } from "./state.js";

export async function load() {
  const d = await (await fetch("/api/data")).json();
  S.doc = d.psyche || {};
  S.doc.encounters = S.doc.encounters || [];
  S.doc.thoughts = S.doc.thoughts || [];
  S.doc.remarks = S.doc.remarks || [];
  S.actionKinds = (d.actions && d.actions.action_kinds) || {};
  S.npcNames = d.npcs || {};
  S.scenes = d.scenes || [];
  S.world = d.world || { levels: {}, placements: {} };
  S.layout = d.layout || {};
  S.connected = true;
  S.dirty = false;
}

export async function save() {
  await fetch("/api/save", { method: "POST",
    body: JSON.stringify({ psyche: S.doc, layout: S.layout }) });
  S.dirty = false;
}
