// SPDX-License-Identifier: MPL-2.0
// Copyright (C) 2026 Henri David
// Emulator part "dim" (web/emu/parts/dim*.js): the template built with it; the lights it shows dim reach the page,
// once while they stay, again when they change, and never a lit one; the page side marks those controls dim.
//   node tests/emu_dim_test.mjs
import { startEmu, buildEmu, check, done } from "./emu_harness.mjs";

const emu = await startEmu(buildEmu("template", "dim"));
emu.run(800);
let dims = emu.take("dim"), frames = emu.take("frame");
const last = dims.at(-1), lit = frames.at(-1).buttons;
check(dims.length >= 1 && last.buttons !== 0, `the glowing buttons reach the page (mask ${last && last.buttons.toString(16)})`);
check((last.buttons & lit) === 0, "none of them lit");
emu.run(500);
check(emu.take("dim").length === 0, "nothing more while they stay");
emu.send({ type: "keys", mask: 1 << 7 });
emu.run(200);
emu.send({ type: "keys", mask: 0 });
emu.run(300);
dims = emu.take("dim");
frames = emu.take("frame");
check(dims.every((d) => (d.keys & frames.at(-1).keys) === 0), "a lit key is never sent as dim");

// the page side: dim.js on stand-ins for index.html's controls
const classes = () => { const s = new Set(); return { toggle: (c, on) => (on ? s.add(c) : s.delete(c)), has: (c) => s.has(c) }; };
globalThis.document = { createElement: () => ({}), head: { appendChild() {} } };
globalThis.buttons = { FX: { bit: 0, g: { classList: classes() } }, SEL: { bit: 1, g: { classList: classes() } } };
globalThis.keys = [{ g: { classList: classes() } }, { g: { classList: classes() } }];
const page = await import("../web/emu/parts/dim.js");
page.attach();
page.message({ buttons: 2, keys: 1 });
check(!buttons.FX.g.classList.has("dim") && buttons.SEL.g.classList.has("dim") && keys[0].g.classList.has("dim")
  && !keys[1].g.classList.has("dim"), "the page marks the dim button and key");
page.message({ buttons: 0, keys: 0 });
check(!buttons.SEL.g.classList.has("dim") && !keys[0].g.classList.has("dim"), "and unmarks them");
// the dim layer's brightness (APP_DIM_LEVELS, plat_dim_ns): tests/dim_app built with the part; its pulse reaches the page
// with the masks, again when it changes, and an app without plat_dim_ns sends none
check(dims.every((d) => d.ns === undefined), "the template's messages carry no ns");
const lv = await startEmu(buildEmu("tests/dim_app", "dim"));
lv.run(500);
let m = lv.take("dim").at(-1);
check(m && m.ns === 3200, `the glow's own pulse before the app sets one (${m && m.ns})`);
for (const [bit, ns] of [[0, 500], [1, 1000], [2, 2000], [3, 4000], [4, 500]]) {
  lv.send({ type: "buttons", mask: 1 << bit });
  lv.run(200);
  lv.send({ type: "buttons", mask: 0 });
  lv.run(200);
  m = lv.take("dim").at(-1);
  check(m && m.ns === ns, `button ${bit}: ${ns} ns reaches the page (${m && m.ns})`);
}
lv.run(500);
check(lv.take("dim").length === 0, "nothing more while the pulse stays");
lv.send({ type: "buttons", mask: 1 << 5 });
lv.run(200);
m = lv.take("dim").at(-1);
check(m && m.ns === 3200, `handed back: the glow's pulse again (${m && m.ns})`);

// the page side: the opacity follows the pulse, faintest at 500 ns, full at 4,000, clamped
const props = {};
globalThis.document.documentElement = { style: { setProperty: (k, v) => { props[k] = v; } } };
check(page.opacity(500) === 0.25 && page.opacity(4000) === 1 && page.opacity(100) === 0.25 && page.opacity(9000) === 1,
  "opacity: 0.25 at 500 ns, 1 at 4,000, clamped");
check(page.opacity(1000) < page.opacity(2000) && page.opacity(2000) < page.opacity(3200), "and rises with the pulse");
page.message({ buttons: 0, keys: 0, ns: 500 });
check(props["--dimop"] === "0.250", "the page sets --dimop from ns");
props["--dimop"] = "x";
page.message({ buttons: 0, keys: 0 });
check(props["--dimop"] === "x", "a message without ns leaves it alone");
done();
