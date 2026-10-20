/* Reusable T9 replay and interactive keypad. Load trace.js and live-engine.js first.
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
(() => {
  "use strict";
  const scenes = [
    { label: "Prediction", title: "Predictive input", detail: "Output: hello world", result: "hello world" },
    { label: "Selection", title: "Candidate cycling", detail: "Sequence: 4663. Selected word: home", result: "home" },
    { label: "Completion", title: "Staged completion", detail: "Word family: keyboard, keyboards", result: "keyboards" },
    { label: "Suggestions", title: "Word suggestions", detail: "Enter accepts goods; 0 accepts only good" },
    { label: "Symbols", title: "Symbol chords", detail: "Hold phone 1 and tap keys 2–5 to choose symbols" },
    { label: "Shortcuts", title: "Editing and application shortcuts", detail: "Hold / or M1 to access shortcuts" },
    { label: "Fn layer", title: "Function layer", detail: "Hold Fn for lighting, media, connections and suggestions" },
  ];
  const inputLabels = { ENTER: "Enter", DIAL: "Dial ↻", DOT: ".", SPELL: "+", TAB: "Tab",
    HOLD_FN: "Hold Fn", RELEASE_FN: "Release Fn", HOLD_1: "Hold 1", RELEASE_1: "Release 1",
    HOLD_SLASH: "Hold /", RELEASE_SLASH: "Release /", HOLD_M1: "Hold M1", RELEASE_M1: "Release M1" };
  const layerNames = { original: "Original legends · numpad order", t9: "T9 legends · phone order",
    symbols: "Hold phone 1 · symbol chords", slash: "Hold / · editing shortcuts",
    "slash-switch": "Hold / · application switching", m1: "Hold M1 · terminal shortcut", fn: "Hold Fn · function layer", numpad: "Numpad mode · printed digits",
    multitap: "Multitap · tap repeatedly to choose a letter", spelling: "Spelling · tap repeatedly to choose a letter" };
  const groups = { 1: ". , ? !", 2: "ABC", 3: "DEF", 4: "GHI", 5: "JKL", 6: "MNO", 7: "PQRS", 8: "TUV", 9: "WXYZ" };
  const style = `
    :host { --t9-accent: #d95629; --ink: #2b3029; --muted: #72786d; display: block; container-type: inline-size; color: var(--ink); font-family: -apple-system, BlinkMacSystemFont, "Segoe UI", sans-serif; }
    * { box-sizing: border-box; }
    button, select, input { font: inherit; }
    button, select { cursor: pointer; }
    button { color: inherit; }
    button:focus-visible, select:focus-visible, input:focus-visible { outline: 3px solid var(--t9-accent); outline-offset: 4px; }
    .mono { font-family: ui-monospace, SFMono-Regular, Menlo, monospace; }
    .shell { border: 1px solid #d9dcd0; border-radius: 14px; overflow: hidden; background: #fffdf9; box-shadow: 0 8px 28px #343a2205; }
    .topline { display: flex; align-items: center; justify-content: space-between; padding: 17px 24px; border-bottom: 1px solid #e7e8df; font-size: 9px; letter-spacing: 1.2px; color: var(--muted); }
    .scene-controls { display: flex; align-items: center; justify-content: space-between; flex-wrap: wrap; gap: 12px; padding: 13px 24px; border-bottom: 1px solid #e7e8df; }
    .scene-buttons { display: flex; flex-wrap: wrap; gap: 6px; }
    .scene-button { border: 1px solid #e1e4d8; border-radius: 5px; background: #f6f7f0; padding: 8px 10px; font-size: 11px; }
    .scene-button:hover { border-color: #aab39d; }
    .scene-button[aria-pressed=true] { background: #303b2a; border-color: #303b2a; color: #fcfcf4; }
    .loop-label { display: inline-flex; align-items: center; gap: 7px; font-size: 11px; color: #637156; cursor: pointer; }
    .loop-scene { margin: 0; accent-color: #637156; }
    .stage { display: grid; grid-template-columns: 1fr 1.05fr; min-height: 420px; }
    .hardware { position: relative; display: flex; flex-direction: column; align-items: center; justify-content: center; padding: 27px 24px 22px; background: radial-gradient(ellipse at 50% 54%, #e4e5d9 0, #f2f2ea 52%, #f5f5ef 78%); border-right: 1px solid #e7e8df; }
    .hardware::before { content: ""; position: absolute; inset: 0; opacity: .23; background-image: radial-gradient(#b2b7a6 .6px, transparent .6px); background-size: 12px 12px; pointer-events: none; }
    .keyboard { position: relative; width: min(360px, 100%); padding: 16px 14px 12px; border: 1px solid #454545; border-radius: 14px 14px 17px 17px; background: #303030; box-shadow: 0 6px 0 #222, 0 7px 0 #555, 0 19px 25px -9px #0005, inset 0 1px 1px #ffffff22; transform: perspective(1000px) rotateX(7deg); }
    .keygrid { display: grid; grid-template-columns: repeat(5, 1fr); grid-template-rows: repeat(6, 55px); gap: 8px 6px; }
    .key { position: relative; display: flex; flex-direction: column; justify-content: center; align-items: center; border-radius: 5px; border: 1px solid #898989; background: #737373; color: #fff; box-shadow: 0 3px 0 #222, inset 0 1px 1px #ffffff18; transition: transform .15s, background .15s, box-shadow .15s; }
    .key.macro { background: #505050; border-color: #686868; }
    .key.active { transform: translateY(3px); background: #858585; border-color: #ddd; box-shadow: 0 0 0 1px #ffffff55; }
    .key { overflow: hidden; padding: 0; user-select: none; touch-action: none; }
    .key:disabled, .knob:disabled { opacity: 1; cursor: default; }
    .key:enabled:hover { border-color: #ddd; }
    .mode-button, .hold-button, .reset-live, .dial-button { border: 1px solid #d5dacb; border-radius: 5px; background: #f6f7f0; padding: 7px 10px; font-size: 11px; letter-spacing: 0; }
    .mode-button[aria-pressed=true], .hold-button[aria-pressed=true] { background: #935c3d; color: white; border-color: #935c3d; }
    .mode-button { min-width: 90px; }
    .keyboard:focus-visible { outline: 3px solid var(--t9-accent); outline-offset: 5px; }
    .dial-controls { display: flex; gap: 8px; height: 32px; margin-top: 10px; visibility: hidden; }
    .is-live .dial-controls { visibility: visible; }
    .live-tools { display: none; align-items: center; gap: 8px; flex-wrap: wrap; }
    .is-live .transport > :not(.live-tools) { display: none; }
    .is-live .live-tools { display: flex; }
    .legend-face { position: absolute; inset: 0; display: flex; flex-direction: column; align-items: center; justify-content: center; }
    .legend-face.entering { animation: legend-in var(--legend-duration) ease-in-out both; animation-play-state: var(--legend-play-state); }
    .legend-face.leaving { transform: translateX(-110%); animation: legend-out var(--legend-duration) ease-in-out both; animation-play-state: var(--legend-play-state); }
    @keyframes legend-out { from { transform: translateX(0); } to { transform: translateX(-110%); } }
    @keyframes legend-in { from { transform: translateX(110%); } to { transform: translateX(0); } }
    .key.held { border-color: #f1bb87; background: #935c3d; box-shadow: 0 0 0 1px #dca47f; }
    .layer-legend { font-size: 12px; line-height: 1.25; text-align: center; padding: 7px 2px 2px; }
    .layer-legend .physical { font-size: 9px; }
    .layer-legend .unassigned { opacity: .45; }
    .key .large { font-size: 28px; font-weight: 550; line-height: 1.1; }
    .key .symbol-large { font-size: 32px; }
    .key .letters { font-size: 12px; line-height: 1.15; letter-spacing: .4px; margin-top: 3px; }
    .key .clear-label { font-size: 14px; letter-spacing: 0; }
    .key .small { font-size: 16px; font-weight: 500; }
    .key.macro .large { font-size: 16px; font-weight: 500; }
    .key .physical { position: absolute; top: 2px; right: 3px; font-size: 10px; opacity: .7; }
    .knob { width: 58px; height: 58px; touch-action: none; align-self: center; justify-self: center; transform: translate(-8px, -2px); border-radius: 50%; background: repeating-conic-gradient(#202020 0deg 4deg, #080808 4deg 8deg); border: 2px solid #b8b8b8; box-shadow: 0 2px 0 #080808, 0 3px 5px #0005; padding: 4px; transition: border-color .2s; }
    .knob-face { width: 100%; height: 100%; border: 1px solid #333; border-radius: 50%; background: #101010; transition: transform .4s; position: relative; }
    .knob-face::before { content: ""; position: absolute; width: 2px; height: 6px; border-radius: 2px; top: 3px; left: calc(50% - 1px); background: #fff; }
    .knob.active { border-color: #ddd; }
    .hardware-note { position: relative; margin-top: 25px; height: 24px; line-height: 12px; text-align: center; font-size: 9px; color: #777f6d; letter-spacing: .2px; }
    .dial-note { position: relative; margin-top: 6px; height: 28px; line-height: 14px; text-align: center; font-size: 9px; color: #777f6d; }
    .hardware-note b { font-weight: 500; color: #4c5a40; }
    .readout { padding: 27px 30px 22px; min-width: 0; }
    .section-label { display: flex; align-items: center; justify-content: space-between; color: var(--muted); font-size: 9px; letter-spacing: 1.1px; text-transform: uppercase; }
    .section-label .count { text-transform: none; letter-spacing: 0; font-size: 9px; color: #92988a; }
    /* Keep playback geometry stable; long sequences scroll within two rows. */
    .digits { display: flex; gap: 5px; align-items: flex-start; align-content: flex-start; height: 73px; overflow-y: auto; scrollbar-gutter: stable; margin: 10px 0 22px; flex-wrap: wrap; }
    .digit { display: grid; place-items: center; min-width: 29px; height: 34px; padding: 0 5px; border: 1px solid #dddcd0; background: #f5f4ee; border-radius: 5px; font-size: 15px; }
    .digit.action { padding: 0 8px; font-size: 12px; }
    .digit:last-child { border-color: #e9ba9c; color: #a94a23; background: #fcf0e4; }
    .placeholder { font-size: 11px; color: #92978c; }
    .candidates { position: relative; height: 69px; overflow-y: auto; scrollbar-gutter: stable; display: flex; align-content: flex-start; flex-wrap: wrap; gap: 6px; margin: 11px 0 18px; }
    .candidate { display: inline-flex; align-items: center; height: 27px; padding: 0 9px; background: #f5f5ef; border: 1px solid #e5e6db; border-radius: 5px; font-size: 11px; color: #747b69; }
    .candidate.selected { color: #96502b; background: #fff0e0; border-color: #edbc99; }
    .candidate .suffix { opacity: .65; }
    .host { border: 1px solid #dfe2d4; border-radius: 9px; background: #f3f5ed; padding: 15px 17px; height: 252px; display: grid; grid-template-rows: 12px minmax(0, 1fr) 24px; gap: 10px; }
    .host .section-label { font-size: 8px; }
    .output { position: relative; min-width: 0; min-height: 0; overflow-x: hidden; overflow-y: auto; overflow-wrap: anywhere; font-size: clamp(19px, 2.3vw, 27px); letter-spacing: -1px; line-height: 1.4; white-space: break-spaces; }
    .preview { color: #a6512b; text-decoration: underline; text-decoration-color: #dca47f; text-underline-offset: 6px; text-decoration-thickness: 1px; }
    .cursor { display: inline-block; height: 1em; width: 2px; margin-left: 2px; transform: translateY(3px); background: var(--t9-accent); animation: blink 1.2s step-end infinite; }
    @keyframes blink { 50% { opacity: 0; } }
    .edits { overflow: auto; line-height: 12px; font-size: 8px; letter-spacing: .15px; color: #858d7a; }
    .action-output { height: 104px; margin-top: 14px; padding: 12px 14px; border: 1px solid #edbc99; border-radius: 7px; background: #fff5e9; }
    .action-output[hidden] { display: block; visibility: hidden; }
    .action-text { height: 60px; overflow: auto; margin-top: 7px; font-size: 12px; line-height: 1.5; }
    .caption { display: flex; align-items: flex-start; gap: 18px; padding: 16px 24px; height: 100px; border-top: 1px solid #e5e7dc; background: #fafaf5; }
    .caption > div { min-width: 0; }
    .caption-index { color: var(--t9-accent); font-size: 11px; white-space: nowrap; }
    .caption-title { height: 16px; line-height: 16px; font-size: 12px; font-weight: 600; margin-bottom: 4px; }
    .caption-detail { height: 46px; overflow: auto; font-size: 11px; color: #777f6c; line-height: 1.5; }
    .transport { height: 64px; display: flex; align-items: center; gap: 14px; padding: 15px 22px; border-top: 1px solid #e5e7dc; }
    .play { display: inline-flex; align-items: center; justify-content: center; gap: 8px; background: #303b2a; color: #fcfcf4; border: 0; border-radius: 6px; min-width: 90px; height: 33px; font-size: 11px; }
    .play:hover { background: #49583f; }
    .icon-button { display: grid; place-items: center; background: transparent; border: 0; padding: 4px; width: 27px; height: 33px; color: #6b755f; }
    .icon-button:hover { color: var(--t9-accent); }
    svg { display: block; width: 16px; height: 16px; stroke: currentColor; stroke-width: 1.6; fill: none; stroke-linecap: round; stroke-linejoin: round; }
    .scrub { display: flex; align-items: center; gap: 12px; flex: 1; min-width: 0; }
    input[type=range] { width: 100%; min-width: 50px; height: 16px; accent-color: #83916e; cursor: pointer; }
    .frame { font-size: 9px; color: #858c7c; white-space: nowrap; min-width: 40px; }
    select { background: #f6f7f0; border: 1px solid #e1e4d8; border-radius: 5px; color: #637156; font-size: 10px; height: 29px; padding: 0 5px; }
    .sr-only { position: absolute; width: 1px; height: 1px; overflow: hidden; clip-path: inset(50%); white-space: nowrap; }
    @container (max-width: 900px) {
      .stage { grid-template-columns: 1fr 1fr; }
      .hardware { padding: 24px 15px; }
      .readout { padding: 24px 19px; }
      .hardware-note { font-size: 8px; }
      .candidates { height: 93px; }
      .action-output { height: 128px; }
      .action-text { height: 84px; }
    }
    @container (max-width: 820px) {
      .stage { grid-template-columns: 1fr; }
      .hardware { border-right: 0; border-bottom: 1px solid #e7e8df; padding: 25px 16px 18px; }
      .keyboard { width: min(360px, 100%); }
      .hardware-note { margin-top: 21px; }
      .readout { padding: 22px; }
      .digits { margin-bottom: 17px; }
      .candidates { margin-bottom: 12px; }
      .output { font-size: 26px; }
      .topline { padding: 14px 16px; font-size: 8px; letter-spacing: .6px; }
      .scene-controls { padding: 12px 16px; }
      .caption { padding: 16px; gap: 12px; height: 124px; }
      .caption-detail { height: 70px; }
      .transport { padding: 13px 12px; gap: 7px; }
      .play { min-width: 74px; }
      .scrub { gap: 4px; }
      .frame { font-size: 8px; min-width: 30px; }
    }
    @container (max-width: 400px) {
      .keyboard { padding: 11px 9px 9px; }
      .keygrid { grid-template-rows: repeat(6, 46px); gap: 6px 4px; }
      .knob { width: 48px; height: 48px; }
      .key .large { font-size: 24px; }
      .key .small, .key.macro .large { font-size: 13px; }
      .key .letters, .layer-legend { font-size: 10px; }
      .key .physical { font-size: 8px; }
      .transport { height: 96px; }
      .topline { flex-wrap: wrap; gap: 8px; }
    }
    @container (max-width: 340px) { .frame { display: none; } }
    @media (prefers-reduced-motion: reduce) { *, *::before { animation: none !important; transition: none !important; } .keyboard { transform: none; } }
  `;
  const icons = {
    play: '<svg viewBox="0 0 20 20" aria-hidden="true"><path d="m7 4 9 6-9 6Z" fill="currentColor" stroke="none"/></svg>',
    pause: '<svg viewBox="0 0 20 20" aria-hidden="true"><path d="M7 5v10M13 5v10" stroke-width="2.5"/></svg>',
    replay: '<svg viewBox="0 0 20 20" aria-hidden="true"><path d="M4 7a7 7 0 1 1 0 7M4 3v4h4"/></svg>',
    step: '<svg viewBox="0 0 20 20" aria-hidden="true"><path d="m4 5 8 5-8 5ZM15 5v10"/></svg>',
  };
  function key(label, sub, action, column, row, extra = "", physical = "", original = physical || label,
               originalSub = label === "M5" ? "Fn" : action === "ENTER" ? "ENTER" : "") {
    const span = action === "0" ? " / span 2" : "";
    const tall = label === "+" || action === "ENTER" ? " / span 2" : "";
    if (/^M[1-5]$/.test(label)) extra += " macro";
    const remapped = String(original) !== String(label) || originalSub !== sub;
    const originalFace = `<span class="${String(original).length === 1 ? "large" : "small"}${["△", "×"].includes(original) ? " symbol-large" : ""}">${original}</span>${originalSub ? `<span class="letters${originalSub === "clear" ? " clear-label" : ""}">${originalSub}</span>` : ""}`;
    const position = physical || ({ "↵": "Ent", "·": ".", "−": "-", M5: "Fn" }[label] || label);
    const t9Face = remapped ? `${physical ? `<span class="physical">${physical}</span>` : ""}<span class="${sub ? "large" : "small"}">${label}</span>${sub ? `<span class="letters">${sub}</span>` : ""}` : originalFace;
    return `<button type="button" disabled class="key ${extra}" aria-label="${position} key" data-key="${action || String(label).toUpperCase()}" data-position="${position}" style="grid-column:${column}${span};grid-row:${row}${tall}">
      <template class="original-template">${originalFace}</template>
      <template class="t9-template">${t9Face}</template>
      <span class="legend-face">${originalFace}</span>
    </button>`;
  }
  function keyboard() {
    let keys = '<button type="button" disabled class="knob" data-key="DIAL" aria-label="Turn dial clockwise"><span class="knob-face" style="display:block"></span></button>';
    const symbols = ["○", "△", "□", "×"];
    ["Esc", "Del", "Tab", "Bksp"].forEach((label, i) => { keys += key(label, "", "", i + 2, 1, "utility", "", symbols[i]); });
    ["M1", "Num", "/", "*", "−"].forEach((label, i) => {
      keys += label === "Num"
        ? key(label, "", "", i + 1, 2, "utility", "", "num", "clear")
        : key(label, "", "", i + 1, 2, "utility");
    });
    for (let row = 0; row < 3; row++) {
      keys += key(`M${row + 2}`, "", "", 1, row + 3, "utility");
      for (let col = 0; col < 3; col++) {
        const digit = row * 3 + col + 1;
        const printed = (2 - row) * 3 + col + 1;
        keys += key(digit, groups[digit], digit, col + 2, row + 3, "legend", printed);
      }
    }
    keys += key("+", "SPELL", "SPELL", 5, 3, "utility");
    keys += key("↵", "ENTER", "ENTER", 5, 5, "accept", "", "enter", "");
    keys += key("M5", "Fn", "FN", 1, 6, "utility");
    keys += key("0", "ACCEPT + SPACE", "0", 2, 6, "legend");
    keys += key("·", "", "DOT", 4, 6, "utility");
    return `<div class="keyboard" tabindex="-1" data-layout="original" aria-label="K0 Max keypad"><div class="keygrid">${keys}</div></div>`;
  }

  class T9Demo extends HTMLElement {
    constructor() {
      super();
      this.attachShadow({ mode: "open" });
      this.sceneIndex = 0;
      this.frameIndex = 0;
      this.speed = 1;
      this.playing = false;
      this.loopScene = false;
      this.timer = null;
      this.liveMode = false;
      this.liveLoading = false;
      this.live = null;
      this.heldSources = new Map();
      this.pointerKeys = new Map();
      this.keyboardKeys = new Map();
      this.visualPresses = new Map();
      this.motion = matchMedia("(prefers-reduced-motion: reduce)");
    }

    connectedCallback() {
      this.data = window.T9_DEMO_TRACE;
      if (!this.data?.scenes?.length) {
        this.shadowRoot.textContent = "Load trace.js before t9-demo.js to display the engine replay.";
        return;
      }
      this.events = new AbortController();
      const options = { signal: this.events.signal };
      this.shadowRoot.innerHTML = `<style>${style}</style>
        <section class="shell" aria-label="Recorded engine playback">
          <div class="topline mono"><span class="record">T9 K0 engine demo</span><button type="button" class="mode-button" aria-pressed="false">Try it</button><span class="dictionary"></span></div>
          <div class="scene-controls">
            <div class="scene-buttons" role="group" aria-label="Choose a scene">
              ${scenes.map((scene, index) => `<button type="button" class="scene-button" data-scene="${index}" aria-pressed="false">${scene.label}</button>`).join("")}
            </div>
            <label class="loop-label"><input class="loop-scene" type="checkbox">Loop this scene</label>
          </div>
          <div class="stage">
            <div class="hardware">${keyboard()}<div class="hardware-note"></div><div class="dial-note"></div><div class="dial-controls"><button type="button" class="dial-button" data-turn="-1" aria-label="Turn dial counter-clockwise">↶ Turn left</button><button type="button" class="dial-button" data-turn="1" aria-label="Turn dial clockwise">Turn right ↷</button></div></div>
            <div class="readout">
              <div class="section-label mono"><span>1. Input sequence</span><span class="count key-hint">Ready</span></div>
              <div class="digits mono"></div>
              <div class="section-label mono"><span class="candidate-label">2. Candidates</span><span class="count candidate-count"></span></div>
              <div class="candidates mono"></div>
              <div class="host"><div class="section-label mono"><span>3. Host text</span><span>Live preview</span></div><div class="output mono"><span class="committed"></span><span class="preview"></span><span class="cursor" aria-hidden="true"></span><span class="after"></span></div><div class="edits mono"></div></div>
              <div class="action-output" hidden><div class="section-label mono">Action preview</div><div class="action-text"></div></div>
            </div>
          </div>
          <div class="caption"><span class="caption-index mono"></span><div><div class="caption-title"></div><div class="caption-detail"></div></div></div>
          <div class="transport">
            <div class="live-tools" role="group" aria-label="Interactive keypad controls">
              <button type="button" class="reset-live">Reset</button>
              ${[[11, '1'], [7, '/'], [5, 'M1'], [23, 'Fn']].map(([position, label]) => `<button type="button" class="hold-button" data-hold="${position}" aria-pressed="false">Hold ${label}</button>`).join('')}
            </div>
            <button class="play" aria-label="Play animation"></button>
            <button class="icon-button replay" aria-label="Replay current scene" title="Replay current scene">${icons.replay}</button>
            <div class="scrub"><input type="range" min="0" value="0" step="1" aria-label="Animation frame"><span class="frame mono"></span></div>
            <button class="icon-button step" aria-label="Next frame" title="Next frame">${icons.step}</button>
            <select aria-label="Playback speed"><option value="0.5">0.5×</option><option value="1" selected>1×</option><option value="1.5">1.5×</option><option value="2">2×</option></select>
          </div>
        </section><div class="sr-only announcement" role="status" aria-live="polite"></div>`;
      this.$(".dictionary").textContent = `${this.data.wordCount.toLocaleString("en-US")} ENGLISH WORDS`;
      this.shadowRoot.addEventListener("click", (event) => {
        const button = event.target.closest("button");
        if (!button) return;
        if (button.hasAttribute("data-scene")) this.setScene(Number(button.dataset.scene));
        if (button.classList.contains("play")) this.playing ? this.pause() : this.play();
        if (button.classList.contains("replay")) this.restart();
        if (button.classList.contains("step")) { this.pause(); this.advance(); this.announce(); }
      }, options);
      this.$('input[type="range"]').addEventListener("input", (event) => {
        this.pause();
        this.frameIndex = Number(event.target.value);
        this.render();
        this.announce();
      }, options);
      this.$(".loop-scene").checked = this.loopScene;
      this.$(".loop-scene").addEventListener("change", (event) => {
        this.loopScene = event.target.checked;
      }, options);
      this.$("select").value = String(this.speed);
      this.$("select").addEventListener("change", (event) => {
        this.speed = Number(event.target.value);
        this.schedule();
      }, options);
      document.addEventListener("visibilitychange", () => this.schedule(), options);
      this.motion.addEventListener("change", () => { if (this.motion.matches) this.pause(); }, options);
      this.bindLiveInput(options);
      this.render();
      if (this.hasAttribute("autoplay") && !this.motion.matches) this.play();
    }

    disconnectedCallback() {
      clearTimeout(this.timer);
      clearTimeout(this.keyTimer);
      clearInterval(this.liveTimer);
      this.releaseLiveKeys();
      this.liveMode = false;
      this.liveRequest = (this.liveRequest || 0) + 1;
      this.events?.abort();
      this.playing = false;
    }

    $(selector) { return this.shadowRoot.querySelector(selector); }

    async enterLive() {
      if (this.liveLoading || this.liveMode) return;
      this.pause();
      const request = this.liveRequest = (this.liveRequest || 0) + 1;
      this.liveLoading = true;
      this.$('.mode-button').disabled = true;
      this.$('.caption-detail').textContent = 'Loading the interactive keypad…';
      try {
        if (!this.live) this.live = await window.T9LiveEngine.create();
        if (!this.isConnected || request !== this.liveRequest) return;
        this.liveMode = true;
        this.render();
        this.liveTimer = setInterval(() => {
          if (this.liveMode && !document.hidden && this.live.tick()) this.render();
        }, 50);
        this.$('.keyboard').focus({ preventScroll: true });
        this.announce();
      } catch (error) {
        if (this.isConnected && request === this.liveRequest) {
          this.$('.caption-detail').textContent = 'The interactive engine could not load. Check that engine-wasm.js and live-engine.js are beside this page, then try again.';
          this.$('.announcement').textContent = 'Interactive keypad unavailable. Recorded scenes are still available.';
        }
      } finally {
        this.liveLoading = false;
        this.$('.mode-button').disabled = false;
      }
    }

    leaveLive() {
      this.liveRequest = (this.liveRequest || 0) + 1;
      clearInterval(this.liveTimer);
      this.releaseLiveKeys();
      this.liveMode = false;
      this.liveLoading = false;
      this.render();
    }

    releaseLiveKeys() {
      for (const timer of this.visualPresses.values()) clearTimeout(timer);
      this.visualPresses.clear();
      this.heldSources.clear();
      this.pointerKeys.clear();
      this.keyboardKeys.clear();
      this.live?.releaseAll();
    }

    liveKey(position, down, source) {
      if (!this.liveMode) return;
      let sources = this.heldSources.get(position);
      if (down) {
        if (!sources) this.heldSources.set(position, sources = new Set());
        if (sources.has(source)) return;
        if (!sources.size) {
          this.live.key(position, true);
          this.showLivePress(position);
        }
        sources.add(source);
      } else {
        if (!sources?.delete(source)) return;
        if (!sources.size) { this.live.key(position, false); this.heldSources.delete(position); }
      }
      this.render();
      if (!down) this.announce();
    }

    showLivePress(position) {
      // Let a quick tap finish depressing the key without extending engine input.
      // A real hold keeps the key depressed after this minimum visual duration.
      const name = window.T9LiveEngine.positions[position];
      clearTimeout(this.visualPresses.get(name));
      this.visualPresses.set(name, setTimeout(() => {
        this.visualPresses.delete(name);
        if (this.liveMode) this.$(`[data-key="${name}"]`).classList.toggle('active', Boolean(this.heldSources.get(position)?.size));
      }, 200));
    }

    turnLiveDial(clockwise) {
      if (!this.liveMode) return;
      this.live.dial(clockwise);
      this.render();
      this.announce();
    }

    bindLiveInput(options) {
      const positionFor = button => window.T9LiveEngine.positions.indexOf(button.dataset.key);
      this.shadowRoot.addEventListener('click', event => {
        const button = event.target.closest('button');
        if (!button) return;
        if (button.classList.contains('mode-button')) {
          if (this.liveMode) this.leaveLive(); else this.enterLive();
          return;
        }
        if (!this.liveMode) return;
        if (button.classList.contains('reset-live')) {
          this.releaseLiveKeys(); this.live.reset(); this.render(); this.announce();
        }
        if (button.hasAttribute('data-hold')) {
          const position = Number(button.dataset.hold);
          this.liveKey(position, !this.heldSources.get(position)?.has('latch'), 'latch');
        }
        if (button.classList.contains('knob')) { this.turnLiveDial(true); return; }
        if (button.hasAttribute('data-turn')) this.turnLiveDial(Number(button.dataset.turn) > 0);
        // Keyboard/assistive-technology clicks have no pointer gesture.
        if (button.hasAttribute('data-key') && event.detail === 0) {
          const position = positionFor(button);
          this.liveKey(position, true, 'click'); this.liveKey(position, false, 'click');
        }
      }, options);
      this.$('.keyboard').addEventListener('pointerdown', event => {
        const button = event.target.closest('[data-key]');
        if (!this.liveMode || !button || button.classList.contains('knob') || event.button !== 0) return;
        event.preventDefault();
        this.$('.keyboard').focus({ preventScroll: true });
        button.setPointerCapture(event.pointerId);
        const position = positionFor(button);
        this.pointerKeys.set(event.pointerId, position);
        this.liveKey(position, true, `pointer:${event.pointerId}`);
      }, options);
      const releasePointer = event => {
        const position = this.pointerKeys.get(event.pointerId);
        if (position === undefined) return;
        this.pointerKeys.delete(event.pointerId);
        this.liveKey(position, false, `pointer:${event.pointerId}`);
      };
      for (const type of ['pointerup', 'pointercancel', 'lostpointercapture']) this.shadowRoot.addEventListener(type, releasePointer, options);
      this.$('.keyboard').addEventListener('contextmenu', event => { if (this.liveMode) event.preventDefault(); }, options);
      this.$('.knob').addEventListener('wheel', event => {
        if (!this.liveMode || !event.deltaY) return;
        event.preventDefault(); this.turnLiveDial(event.deltaY > 0);
      }, { ...options, passive: false });
      this.shadowRoot.addEventListener('keydown', event => {
        if (!this.liveMode || event.ctrlKey || event.metaKey || event.altKey || event.target.matches('input, select')) return;
        if ([' ', 'Enter'].includes(event.key) && event.target.closest('button')) return;
        const digit = /^\d$/.test(event.key);
        const numpad = event.code.startsWith('Numpad');
        let position;
        if (digit) position = numpad || this.live.wasm.get(0, 0) === 2
          ? window.T9LiveEngine.physical.indexOf(event.key) : window.T9LiveEngine.positions.indexOf(event.key);
        else position = ({ Escape: 1, Delete: 2, Tab: 3, Backspace: 9, '/': 7, '*': 8, '-': 9,
          '+': 14, Enter: 26, ' ': 24, '.': 25, f: 23, F: 23, m: 5, M: 5, n: 6, N: 6 })[event.key];
        // Keep Tab available for normal focus navigation.
        if (event.key === 'Tab') return;
        if (event.key === 'ArrowLeft' || event.key === 'ArrowRight') {
          event.preventDefault(); if (!event.repeat) this.turnLiveDial(event.key === 'ArrowRight'); return;
        }
        if (position === undefined || position < 0) return;
        event.preventDefault();
        if (event.repeat || this.keyboardKeys.has(event.code)) return;
        this.$('.keyboard').focus({ preventScroll: true });
        this.keyboardKeys.set(event.code, position);
        this.liveKey(position, true, `key:${event.code}`);
      }, options);
      window.addEventListener('keyup', event => {
        const position = this.keyboardKeys.get(event.code);
        if (position === undefined) return;
        event.preventDefault(); this.keyboardKeys.delete(event.code);
        this.liveKey(position, false, `key:${event.code}`);
      }, options);
      const release = () => {
        if (this.liveMode) { this.releaseLiveKeys(); if (this.isConnected) this.render(); }
      };
      window.addEventListener('blur', release, options);
      document.addEventListener('visibilitychange', () => { if (document.hidden) release(); }, options);
      this.shadowRoot.addEventListener('focusout', () => {
        queueMicrotask(() => { if (!this.shadowRoot.activeElement) release(); });
      }, options);
    }

    get frames() {
      const frames = this.data.scenes[this.sceneIndex].frames;
      // Show the physical legends before remapping at the start of every scene.
      return [
        { ...frames[0], layout: "original", caption: "Original legends. Numpad order: 7 8 9 / 4 5 6 / 1 2 3." },
        { ...frames[0], layout: "transition", caption: "T9 assignments. Phone order: 1 2 3 / 4 5 6 / 7 8 9." },
        ...frames,
      ];
    }

    play() {
      if (this.liveMode) return;
      this.playing = true;
      this.renderControls();
      this.schedule();
    }

    pause() {
      this.playing = false;
      clearTimeout(this.timer);
      this.renderControls();
    }

    restart() {
      this.frameIndex = 0;
      this.render();
      this.play();
    }

    setScene(index) {
      if (!Number.isInteger(index) || index < 0 || index >= scenes.length) return;
      if (this.liveMode || this.liveLoading) this.leaveLive();
      this.sceneIndex = index;
      this.frameIndex = 0;
      this.render();
      this.announce();
      this.schedule();
    }

    advance() {
      if (this.liveMode) return;
      if (this.frameIndex < this.frames.length - 1) this.frameIndex++;
      else if (this.loopScene) this.frameIndex = 0;
      else if (this.sceneIndex < scenes.length - 1) { this.sceneIndex++; this.frameIndex = 0; }
      else if (this.hasAttribute("loop")) { this.sceneIndex = 0; this.frameIndex = 0; }
      else { this.pause(); return; }
      this.render();
    }

    schedule() {
      clearTimeout(this.timer);
      if (!this.playing || document.hidden || !this.isConnected) return;
      const frame = this.frames[this.frameIndex];
      let delay = 850;
      if (!frame.key) delay = 1400;
      if (this.frameIndex === 2) delay = 500;
      if (frame.layout === "original") delay = 1800;
      if (frame.layout === "transition") delay = 900;
      if (frame.key.startsWith("HOLD_") || frame.key.startsWith("RELEASE_")) delay = 1900;
      if (frame.action) delay = 2400;
      if (["DIAL", "0", "ENTER"].includes(frame.key)) delay = 1700;
      if (frame.family) delay = 1900;
      if (this.frameIndex === this.frames.length - 1) delay = 3000;
      this.timer = setTimeout(() => { this.advance(); this.schedule(); }, delay / this.speed);
    }

    announce() {
      const frame = this.liveMode ? this.live.frame() : this.frames[this.frameIndex];
      this.$(".announcement").textContent = `${this.liveMode ? "Try it" : scenes[this.sceneIndex].detail}. ${frame.caption} ${frame.action || ""}. Host text: ${frame.text || "empty"}`;
    }

    renderControls() {
      const button = this.$(".play");
      if (!button) return;
      button.innerHTML = `${this.playing ? icons.pause : icons.play}<span>${this.playing ? "Pause" : "Play"}</span>`;
      button.setAttribute("aria-label", this.playing ? "Pause animation" : "Play animation");
      this.$(".keyboard").style.setProperty("--legend-play-state", (this.playing || this.liveMode) ? "running" : "paused");
    }

    renderKeyboard(frame) {
      const layout = frame.layout === "original" ? "original" : frame.layer || "t9";
      const board = this.$(".keyboard");
      const animate = (this.playing || this.liveMode) && !this.motion.matches && layout !== "original";
      board.dataset.layout = layout;
      board.tabIndex = this.liveMode ? 0 : -1;
      board.setAttribute("aria-hidden", String(!this.liveMode));
      board.style.setProperty("--legend-duration", `${650 / this.speed}ms`);
      const symbols = { "7": ["Hold 1", "(preview)"], "8": ["( [ { <"], "9": [") ] } >"], "4": [": ; | ~"], "5": ["+ ` @ \\"] };
      const map = layout === "symbols" ? symbols : this.data.layers[layout === "slash-switch" ? "slash" : layout];
      const baseLayout = frame.mode === 2 ? "numpad" : frame.mode === 1 || frame.spelling ? "multitap" : "t9";
      // Keep the large digit legends, but source action labels from the shared keymap.
      const baseLines = position => ["Esc", "Del", "Tab", "Bksp", "Num", "/", "*", "-"].includes(position) || baseLayout === "multitap" && ["+", "Ent"].includes(position)
        ? this.data.layers[baseLayout]?.[position] : undefined;
      board.querySelectorAll(".key").forEach((key) => {
        const position = key.dataset.position;
        let lines = map?.[position];
        if (["t9", "multitap", "spelling"].includes(layout) || lines?.[0] === "(base)") lines = baseLines(position);
        else if (!["original", "numpad"].includes(layout) && !lines) lines = baseLines(position);
        if (layout === "m1") lines = position === "M1" ? ["held"] : position === "Tab" ? ["interrupt", "Ctrl+C"] : ["(none)"];
        if (layout === "slash-switch" && position === "*") lines = ["previous", "app"];
        const face = document.createElement("span");
        face.className = "legend-face";
        if (lines && lines[0] !== "(base)") {
          face.classList.add("layer-legend");
          const physical = document.createElement("span");
          physical.className = "physical";
          physical.textContent = position;
          face.append(physical);
          for (const line of lines.flatMap(line => ["held", "(held)"].includes(line) ? ["held", "(preview)"] : [line])) {
            const label = document.createElement("span");
            label.textContent = line === "(none)" ? "—" : line;
            if (line === "(none)") label.className = "unassigned";
            face.append(label);
          }
        } else {
          face.append(key.querySelector((layout === "original" || layout === "numpad" || frame.mode === 2) ? ".original-template" : ".t9-template").content.cloneNode(true));
        }
        const signature = face.innerHTML;
        const current = key.querySelector(".legend-face:not(.leaving)");
        if (!animate) {
          key.querySelectorAll(".leaving").forEach((old) => old.remove());
          current.classList.remove("entering");
        }
        if (current.innerHTML === signature) return;
        key.querySelectorAll(".leaving").forEach((old) => old.remove());
        if (animate) {
          current.classList.remove("entering");
          current.classList.add("leaving");
          current.addEventListener("animationend", () => current.remove(), { once: true });
          face.classList.add("entering");
        } else current.remove();
        key.append(face);
      });
      this.$(".hardware-note").textContent = layerNames[layout];
      this.$(".dial-note").textContent = layout === "fn" ? "Dial: brightness − / + · press: lighting on/off"
        : layout.startsWith("slash") ? "Dial: ↑ / ↓ · C = ⌘ on Mac, Ctrl on Windows/Linux"
        : layout === "m1" ? "Dial: no action" : layout === "original" ? "" : layout === "numpad" ? "Dial: volume · press: mute" : ["multitap", "spelling"].includes(layout) ? "Dial: character navigation · press: mute" : "Dial: candidates / word navigation · press: mute";
      if (this.liveMode) this.$('.dial-note').textContent = this.$('.dial-note').textContent.replace(/ · press:.*$/, '') + ' · click: clockwise';
      this.shadowRoot.querySelectorAll("[data-key]").forEach((key) => {
        key.disabled = !this.liveMode;
        key.classList.toggle("active", this.liveMode ? frame.pressed.includes(key.dataset.key) || this.visualPresses.has(key.dataset.key) : Boolean(frame.key) && key.dataset.key === frame.key);
        if (this.liveMode) {
          const legend = key.querySelector('.legend-face:not(.leaving)');
          key.setAttribute('aria-label', key.classList.contains('knob') ? 'Turn dial clockwise' : `${key.dataset.position}: ${legend.textContent.trim().replace(/\s+/g, ' ')}`);
        }
        key.classList.toggle("held", Boolean(frame.held) && key.dataset.key === frame.held);
      });
    }

    render() {
      const frame = this.liveMode ? this.live.frame() : this.frames[this.frameIndex];
      const scene = scenes[this.sceneIndex];
      this.$(".shell").classList.toggle("is-live", this.liveMode);
      this.renderKeyboard(frame);
      clearTimeout(this.keyTimer);
      if (this.playing && !this.liveMode && !this.motion.matches) {
        this.keyTimer = setTimeout(() => {
          this.shadowRoot.querySelectorAll("[data-key]").forEach((key) => key.classList.remove("active"));
        }, 320 / this.speed);
      }
      const turns = this.liveMode ? frame.turns : this.frames.slice(0, this.frameIndex + 1).filter((item) => item.key === "DIAL").length;
      this.$(".knob-face").style.transform = `rotate(${turns * 55}deg)`;
      this.$(".key-hint").textContent = frame.key ? inputLabels[frame.key] || `Key ${frame.key}` : "Ready";
      const digits = this.$(".digits");
      digits.replaceChildren();
      // Replay actual input events. Completion can expand frame.digits internally
      // without additional digit presses; those digits only describe engine state.
      const inputs = this.liveMode ? frame.inputs : this.frames.slice(0, this.frameIndex + 1).filter((item) => item.key);
      for (const input of inputs) {
        const chip = document.createElement("span");
        chip.className = `digit${input.key.length > 1 ? " action" : ""}`;
        chip.textContent = input.label || inputLabels[input.key] || input.key;
        digits.append(chip);
      }
      if (!inputs.length) {
        const placeholder = document.createElement("span");
        placeholder.className = "placeholder";
        placeholder.textContent = "No input";
        digits.append(placeholder);
      }
      digits.scrollTop = digits.scrollHeight;
      const list = this.$(".candidates");
      list.replaceChildren();
      // Show a window around the selected candidate without changing engine order.
      const start = Math.max(0, Math.min(frame.selected - 3, frame.candidates.length - 6));
      frame.candidates.slice(start, start + 6).forEach((word, offset) => {
        const chip = document.createElement("span");
        chip.className = `candidate${start + offset === frame.selected ? " selected" : ""}`;
        chip.textContent = word.slice(0, frame.digits.length);
        if ((frame.completions || frame.family) && word.length > frame.digits.length) {
          const suffix = document.createElement("span");
          suffix.className = "suffix";
          const rest = word.slice(frame.digits.length);
          suffix.textContent = frame.completions ? `[${rest}]` : `>${rest}`;
          chip.append(suffix);
        }
        if (start + offset === frame.selected) chip.setAttribute("aria-current", "true");
        list.append(chip);
      });
      if (!frame.candidates.length) {
        const placeholder = document.createElement("span");
        placeholder.className = "placeholder";
        placeholder.textContent = frame.preview ? (frame.spelling || frame.mode === 1 ? "Letter preview" : "Symbol preview") : frame.text ? "Text committed" : "No active candidates";
        list.append(placeholder);
      }
      const selected = list.querySelector(".selected");
      list.scrollTop = selected ? Math.max(0, selected.offsetTop + selected.offsetHeight - list.clientHeight) : 0;
      this.$(".candidate-count").textContent = frame.candidates.length ? `${frame.selected + 1} of ${frame.candidates.length}${frame.candidates.length > 6 ? " · 6 shown" : ""}` : "—";
      this.$(".candidate-label").textContent = frame.completions ? "2. Suggestions" : "2. Candidates";
      this.$(".action-output").hidden = !frame.action;
      this.$(".action-text").textContent = frame.action || "";
      this.$(".committed").textContent = frame.text.slice(0, (frame.cursor ?? frame.text.length) - frame.preview.length);
      this.$(".preview").textContent = frame.preview;
      this.$(".after").textContent = this.liveMode ? frame.text.slice(frame.cursor) : "";
      const output = this.$('.output'), cursor = this.$('.cursor');
      output.scrollLeft = 0;
      const caretBottom = cursor.offsetTop + cursor.offsetHeight + 8;
      if (caretBottom > output.scrollTop + output.clientHeight) output.scrollTop = caretBottom - output.clientHeight;
      else if (cursor.offsetTop < output.scrollTop) output.scrollTop = cursor.offsetTop;
      this.$(".edits").textContent = frame.erased || frame.inserted ? `${frame.erased ? `⌫ ${frame.erased} · ` : ""}type ${JSON.stringify(frame.inserted)}${frame.preview ? " · composing" : " · accepted"}` : "Output: keyboard events";
      this.$(".caption-index").textContent = this.liveMode ? "Try it" : `${this.sceneIndex + 1}.`;
      this.$(".caption-title").textContent = this.liveMode ? `${["Predictive typing", "Multitap", "Numpad"][frame.mode]}${frame.spelling ? " · spelling" : ""}${frame.caps ? " · " + (frame.caps === 1 ? "next capital" : "caps lock") : ""}` : frame.layout ? "Key assignment" : scene.title;
      this.$(".caption-detail").textContent = frame.caption;
      this.shadowRoot.querySelectorAll("[data-scene]").forEach((button) => {
        button.setAttribute("aria-pressed", String(!this.liveMode && Number(button.dataset.scene) === this.sceneIndex));
      });
      this.$(".mode-button").textContent = this.liveMode ? "Watch demo" : "Try it";
      this.$(".mode-button").setAttribute("aria-pressed", String(this.liveMode));
      this.$(".loop-scene").disabled = this.liveMode;
      this.shadowRoot.querySelectorAll('[data-hold]').forEach(button => button.setAttribute('aria-pressed', String(this.heldSources.get(Number(button.dataset.hold))?.has('latch') || false)));
      const slider = this.$('input[type="range"]');
      slider.max = this.frames.length - 1;
      slider.value = this.frameIndex;
      slider.setAttribute("aria-valuetext", `Frame ${this.frameIndex + 1} of ${this.frames.length}: ${frame.caption}`);
      this.$(".frame").textContent = `${String(this.frameIndex + 1).padStart(2, "0")} / ${this.frames.length}`;
      this.renderControls();
      this.dispatchEvent(new CustomEvent("t9-frame", { bubbles: true, composed: true, detail: { scene: this.liveMode ? "live" : this.data.scenes[this.sceneIndex].id, frame: this.liveMode ? null : this.frameIndex, text: frame.text } }));
    }
  }
  if (!customElements.get("t9-demo")) customElements.define("t9-demo", T9Demo);
})();
