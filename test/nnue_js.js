// nnue_js.js - for the Node tests: src/nnue_chess.h and src/nnue_xq.h (if there) as globalThis.NNUE47, the weights
// web/engine.js and web/xq_engine.js take (the page gets them in base64 from tools/build_web.py)
'use strict';
const fs = require('fs'), path = require('path');
function load(file, G) {
  const p = path.join(__dirname, '..', 'src', file);
  if (!fs.existsSync(p)) return null;
  const h = fs.readFileSync(p, 'utf8');
  const num = k => +h.match(new RegExp('#define ' + G + '_' + k + ' (\\d+)'))[1];
  const arr = k => h.match(new RegExp(G + '_' + k + '\\[[^\\]]*\\] = \\{([^}]*)\\}'))[1].split(',').map(Number);
  return { hid: num('HID'), qa: num('QA'), qb: num('QB'), scale: num('SCALE'), w1: arr('W1'), b1: arr('B1'), w2: arr('W2'),
           b2: +h.match(new RegExp(G + '_B2 = (-?\\d+)'))[1] };
}
globalThis.NNUE47 = { chess: load('nnue_chess.h', 'NNC'), xq: load('nnue_xq.h', 'NNX') };
