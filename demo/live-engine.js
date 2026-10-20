/* Browser host for the production C engine. No prediction logic lives here.
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
(() => {
  "use strict";
  const positions = ['DIAL', 'ESC', 'DEL', 'TAB', 'BKSP', 'M1', 'NUM', '/', '*', '−',
    'M2', '1', '2', '3', 'SPELL', 'M3', '4', '5', '6', 'M4', '7', '8', '9', 'FN', '0', 'DOT', 'ENTER'];
  const physical = ['knob', 'Esc', 'Del', 'Tab', 'Bksp', 'M1', 'Num', '/', '*', '-',
    'M2', '7', '8', '9', '+', 'M3', '4', '5', '6', 'M4', '1', '2', '3', 'Fn', '0', '.', 'Ent'];
  const labels = { DIAL: 'Knob', SPELL: '+', DOT: '.', ENTER: 'Enter', ESC: 'Esc', DEL: 'Del', TAB: 'Tab', BKSP: 'Bksp', NUM: 'Num', FN: 'Fn' };
  const actions = ['', '', 'Capitalization changed', 'Keypad division', 'Undo', 'Cut',
    'Copy · Command+C on Mac / Ctrl+C on Windows and Linux', 'Paste', 'Tab', 'Previous field · Shift+Tab',
    'Next app · Command+Tab on Mac / Alt+Tab on Windows and Linux', 'VS Code terminal · Ctrl+backtick',
    'Previous typing mode', 'Down arrow', 'Up arrow', 'Close tab', 'Quit / close window',
    'Interrupt terminal process · Ctrl+C', 'VIA macro M1 (empty by default)'];
  const fnActions = { 100: 'Lighting on/off', 101: 'Bluetooth host 1', 102: 'Bluetooth host 2',
    103: 'Bluetooth host 3', 104: '2.4 GHz connection', 105: 'Bootloader', 106: 'Next lighting effect',
    107: 'Increase brightness', 108: 'Increase hue', 111: 'Previous lighting effect', 112: 'Decrease brightness',
    113: 'Decrease hue', 116: 'Increase saturation', 117: 'Increase effect speed', 118: 'Previous track',
    120: 'Decrease saturation', 121: 'Decrease effect speed', 122: 'Play / pause media', 124: 'Lighting on/off',
    125: 'Next track', 130: 'Increase brightness', 131: 'Decrease brightness', 140: 'Mute',
    141: 'Volume down', 142: 'Volume up', 160: 'Previous app · Command+Shift+Tab / Alt+Shift+Tab',
    161: 'Confirm app selection', 210: 'VIA macro M2 (empty by default)', 215: 'VIA macro M3 (empty by default)',
    219: 'VIA macro M4 (empty by default)' };
  let modulePromise;
  class T9LiveEngine {
    static positions = positions;
    static physical = physical;
    static async create() {
      if (!globalThis.T9_WASM_BASE64) throw new Error('The browser engine asset is missing.');
      if (!modulePromise) {
        const bytes = Uint8Array.from(atob(globalThis.T9_WASM_BASE64), c => c.charCodeAt(0));
        modulePromise = WebAssembly.compile(bytes).catch(error => { modulePromise = null; throw error; });
      }
      const module = await modulePromise;
      if (WebAssembly.Module.imports(module).length) throw new Error('The browser engine must be self-contained.');
      return new T9LiveEngine((await WebAssembly.instantiate(module)).exports);
    }
    constructor(exports) { this.wasm = exports; this.reset(); }
    reset() {
      this.wasm.init(); this.text = ''; this.cursor = 0; this.inputs = []; this.turns = 0;
      this.lastKey = ''; this.events = 0; this.erased = 0; this.inserted = ''; this.hostAction = '';
    }
    string(pointer) {
      const bytes = new Uint8Array(this.wasm.memory.buffer);
      let end = pointer;
      while (bytes[end]) end++;
      return new TextDecoder().decode(bytes.subarray(pointer, end));
    }
    insert(text) {
      this.text = this.text.slice(0, this.cursor) + text + this.text.slice(this.cursor);
      this.cursor += text.length; this.inserted += text;
    }
    erase(start) {
      this.erased += this.cursor - start;
      this.text = this.text.slice(0, start) + this.text.slice(this.cursor); this.cursor = start;
    }
    hostKey(code) {
      if (code >= 256 && code < 512) { this.insert(String.fromCharCode(code - 256)); return; }
      if (code >= 1000) return; // OS actions are previewed, never sent to the browser.
      switch (code) {
        case 1: this.erase(Math.max(0, this.cursor - 1)); break;
        case 2: this.erase(this.wordLeft()); break;
        case 3: this.text = this.text.slice(0, this.cursor) + this.text.slice(this.cursor + 1); break;
        case 4: this.insert('\t'); break;
        case 5: this.insert('\n'); break;
        case 6: this.hostAction = 'Escape'; break;
        case 7: this.cursor = Math.max(0, this.cursor - 1); break;
        case 8: this.cursor = Math.min(this.text.length, this.cursor + 1); break;
        case 9: this.cursor = this.wordLeft(); break;
        case 10: this.cursor += this.text.slice(this.cursor).match(/^\s*\S*/)[0].length; break;
        case 11: this.hostAction = 'Num Lock'; break;
      }
    }
    wordLeft() {
      const before = this.text.slice(0, this.cursor);
      return before.replace(/\s*$/, '').replace(/\S+$/, '').length;
    }
    drain() {
      this.erased = 0; this.inserted = ''; this.events = this.wasm.get(14, 0);
      let changed = Boolean(this.events), item;
      while ((item = this.wasm.pop())) {
        changed = true;
        const kind = item >>> 16, value = item & 65535;
        if (kind === 1) this.insert(String.fromCharCode(value));
        if (kind === 2) this.erase(Math.max(0, this.cursor - 1));
        if (kind === 3) this.hostKey(value);
      }
      return changed;
    }
    record(key, label) {
      this.lastKey = key; this.inputs.push({ key, label });
      if (this.inputs.length > 100) this.inputs.shift();
    }
    key(position, down, now = performance.now()) {
      const wasDown = Boolean(this.wasm.get(13, 0) & (1 << position));
      if (wasDown === down) return;
      this.hostAction = '';
      const key = positions[position];
      const label = this.wasm.get(0, 0) === 2 ? physical[position] : labels[key] || key;
      this.wasm.key(position, down ? 1 : 0, now >>> 0);
      if (down) this.record(key, label);
      else if ([5, 7, 11, 23].includes(position)) this.record('', `Release ${label}`);
      this.drain();
    }
    dial(clockwise, now = performance.now()) {
      this.hostAction = ''; this.wasm.dial(clockwise ? 1 : 0, now >>> 0);
      this.turns += clockwise ? 1 : -1;
      this.record('DIAL', clockwise ? 'Dial ↻' : 'Dial ↺'); this.drain();
    }
    tick(now = performance.now()) { this.wasm.tick(now >>> 0); return this.drain(); }
    releaseAll() { this.wasm.release_all(); this.drain(); }
    frame() {
      const get = id => this.wasm.get(id, 0), mode = get(0), spelling = Boolean(get(1));
      const base = mode === 2 ? 'numpad' : mode === 1 ? 'multitap' : spelling ? 'spelling' : 't9';
      const layer = [base, 'fn', 'slash', 'm1', 'symbols', 'slash-switch'][get(12)];
      const action = get(15);
      const preview = this.string(get(11));
      let message = this.hostAction || actions[action] || fnActions[action] || '';
      if (action === 126) message = get(3) ? 'Word suggestions on' : 'Word suggestions off';
      if (this.events & 1) message = 'No matching word. Hold + to spell it, or press − to edit.';
      if (this.events & 2) message = 'Input queue is busy. Try the key again.';
      const pressed = positions.filter((_, i) => get(13) & (1 << i));
      return {
        key: this.lastKey, text: this.text, cursor: this.cursor, preview,
        digits: Array.from(new Uint8Array(this.wasm.memory.buffer, get(6), get(5))).join(''),
        selected: get(7), family: Boolean(get(8)), completions: Boolean(get(3)),
        candidates: Array.from({ length: get(9) }, (_, i) => this.string(this.wasm.get(10, i))),
        layer, mode, spelling, caps: get(2), pressed, held: ({fn:'FN', slash:'/', 'slash-switch':'/', m1:'M1', symbols:'1'})[layer] || '',
        action: message, erased: this.erased, inserted: this.inserted, inputs: this.inputs, turns: this.turns,
        caption: 'Digits type; Space accepts; Enter completes; ←/→ turn the dial. F = Fn, M = M1, N = Num. Use Hold buttons for mouse chords. OS actions are previews.',
      };
    }
  }
  globalThis.T9LiveEngine = T9LiveEngine;
})();
