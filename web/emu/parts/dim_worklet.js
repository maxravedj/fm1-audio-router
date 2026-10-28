// SPDX-License-Identifier: MPL-2.0
// Copyright (C) 2026 Henri David
// Emulator part "dim" (fm1.toml emu = ["dim"]), the worklet's side: the lights the app shows dim (plat_lights' dim
// buttons and keys, plat_lights_mid's half-lit keys: web_dim_buttons, web_dim_keys) to the page, about 30 times a
// second when they change. An app with APP_DIM_LEVELS (plat_dim_ns) also has them say how bright the dim layer is, the
// pulse each scan frame in ns (web_dim_ns); an app without it sends no ns. The page's side is dim.js.

let last = -1, sent = "";
fm1Part({
  name: "dim",
  process(fm1) {
    if (currentTime - last < 1 / 30) return;
    last = currentTime;
    const buttons = fm1.ex.web_dim_buttons() >>> 0, keys = fm1.ex.web_dim_keys() >>> 0;
    const ns = fm1.ex.web_dim_ns ? fm1.ex.web_dim_ns() >>> 0 : undefined;
    if (`${buttons} ${keys} ${ns}` === sent) return;
    sent = `${buttons} ${keys} ${ns}`;
    fm1.part("dim", ns === undefined ? { buttons, keys } : { buttons, keys, ns });
  },
});
