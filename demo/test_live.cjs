/* Production WASM parity and browser-host behavior. Run: node demo/test_live.cjs
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
const assert = require('node:assert/strict');
global.window = global;
require('./trace.js');
require('./engine-wasm.js');
require('./live-engine.js');
(async () => {
  const engine = await T9LiveEngine.create();
  let now = 0;
  const pos = key => T9LiveEngine.positions.indexOf(key);
  const down = key => engine.key(pos(key), true, now += 100);
  const up = key => engine.key(pos(key), false, now += 40);
  const tap = key => { down(key); up(key); };
  const type = sequence => { for (const key of sequence) tap(key); };
  const reset = () => { engine.reset(); now = 0; };
  const holds = { FN: 'FN', SLASH: '/', M1: 'M1', '1': '1' };
  let compared = 0;
  for (const scene of T9_DEMO_TRACE.scenes) {
    reset();
    for (const recorded of scene.frames) {
      const key = recorded.key;
      if (key.startsWith('HOLD_')) down(holds[key.slice(5)]);
      else if (key.startsWith('RELEASE_')) up(holds[key.slice(8)]);
      else if (key === 'DIAL') engine.dial(true, now += 100);
      else if (key) tap(key);
      const actual = engine.frame();
      for (const field of ['text', 'preview', 'digits', 'selected', 'family', 'completions', 'candidates', 'layer'])
        assert.deepEqual(actual[field], recorded[field], `${scene.id}/${key}: ${field}`);
      compared++;
    }
  }
  reset(); type('435560967530'); assert.equal(engine.text, 'hello world ');
  engine.dial(false, now += 100); assert.equal(engine.cursor, 6);
  type('46630'); assert.equal(engine.text, 'hello good world ');
  tap('BKSP'); assert.equal(engine.text, 'hello world ');
  reset(); down('2'); engine.tick(now += 510); up('2'); assert.equal(engine.text, '2');
  reset(); type('4355'); down('SPELL'); engine.tick(now += 510); up('SPELL');
  assert.equal(engine.frame().spelling, true);
  reset(); tap('/'); type('435560'); assert.equal(engine.text, 'Hello ');
  reset(); tap('NUM'); assert.equal(engine.frame().mode, 1);
  type('22'); assert.equal(engine.frame().preview, 'b');
  engine.tick(now += 900); type('2'); assert.equal(engine.frame().preview, 'ba');
  tap('0'); assert.equal(engine.text, 'ba ');
  tap('NUM'); assert.equal(engine.frame().mode, 2);
  type('123'); assert.equal(engine.text, 'ba 789');
  tap('/'); assert.equal(engine.text, 'ba 789/');
  reset(); down('FN'); down('ENTER'); up('FN'); up('ENTER');
  assert.equal(engine.frame().completions, true); assert.equal(engine.frame().preview, '');
  reset(); down('/'); tap('2'); up('/');
  assert.equal(engine.text, ''); assert.equal(engine.frame().caps, 0);
  reset(); down('1'); engine.tick(now += 600); tap('2'); up('1');
  assert.equal(engine.text, '(');
  reset(); down('M1'); tap('TAB'); up('M1');
  assert.match(engine.frame().action, /Interrupt/);
  reset(); down('/'); tap('SPELL'); tap('*');
  assert.equal(engine.frame().layer, 'slash-switch'); assert.match(engine.frame().action, /Previous app/);
  up('/'); assert.equal(engine.frame().layer, 't9');
  reset(); down('FN'); engine.releaseAll(); assert.deepEqual(engine.frame().pressed, []);
  tap('ENTER'); assert.equal(engine.text, '\n'); assert.equal(engine.frame().completions, false);
  const second = await T9LiveEngine.create();
  assert.equal(second.text, ''); assert.equal(second.frame().completions, false);
  console.log(`Passed: ${compared} recorded frames match the WASM engine; navigation, editing, long holds, caps, spelling, multitap, numpad, shortcuts, release order and instance isolation.`);
})().catch(error => { console.error(error); process.exitCode = 1; });
