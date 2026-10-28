// SPDX-License-Identifier: MPL-2.0
// Copyright (C) 2026 Henri David
// Emulator part "dim" (fm1.toml emu = ["dim"]): the buttons and keys the app lights dimly, drawn dim on the panel
// (a lit one stays lit). With APP_DIM_LEVELS the message carries the dim layer's pulse (ns) and they are drawn the
// brighter the longer it is: 500 ns the faintest, 4,000 the brightest (--dimop below). The worklet's side is
// dim_worklet.js.
// It draws on the page's own panel (index.html's buttons and keys).

const STYLE = `
.dim:not(.lit) .btnface { fill: #6a4535; opacity: var(--dimop, 1); }
.dim:not(.lit) .keyface.white { fill: #e4c6b2; opacity: var(--dimop, 1); }
.dim:not(.lit) .keyface.black { fill: #5d9d96; opacity: var(--dimop, 1); }`;

/** the dim controls' opacity for a pulse of ns: 0.25 at 500 ns up to 1 at 4,000 (a plain linear step: the page's
 *  view of the levels, not a measure of the light) */
export function opacity(ns) {
  const t = Math.min(4000, Math.max(500, ns));
  return 0.25 + 0.75 * (t - 500) / 3500;
}

export function attach() {
  const s = document.createElement("style");
  s.textContent = STYLE;
  document.head.appendChild(s);
}

function show(c, dim) {
  if (c && c.g) c.g.classList.toggle("dim", dim);
}

/** the page's controls: index.html's buttons and keys (none on a page without them) */
function controls() {
  const b = typeof buttons === "object" ? Object.values(buttons) : [];
  const k = typeof keys === "object" && Array.isArray(keys) ? keys : [];
  return { b, k };
}

export function message(m) {
  if (typeof m.ns === "number") document.documentElement.style.setProperty("--dimop", opacity(m.ns).toFixed(3));
  const { b, k } = controls();
  for (const x of b) show(x, !!(m.buttons >>> x.bit & 1));
  k.forEach((x, i) => show(x, !!(m.keys >>> i & 1)));
}
