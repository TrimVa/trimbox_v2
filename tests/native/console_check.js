// ============================================================================
//  Banc v2 §9 n°7 — même algorithme, deux implémentations (v2 §10.8).
//  Rejoue les points générés par test_lap dans les fonctions EXTRAITES de la
//  console (index.html, sans modification) et compare aux événements du
//  firmware. Échec si un temps diffère de plus de 1 ms.
//  Usage : node console_check.js <index.html> <lap_cases.json>
// ============================================================================
const fs = require('fs');
const [,, htmlPath = '../../index.html', casesPath = 'lap_cases.json'] = process.argv;
const html = fs.readFileSync(htmlPath, 'utf8');
const src = html.match(/<script>([\s\S]*?)<\/script>/g).map(s => s.slice(8, -9)).join('\n');

function extractFn(name){
  const i = src.search(new RegExp('function\\s+' + name + '\\s*\\('));
  if(i < 0) throw new Error('fonction absente de la console : ' + name);
  let j = src.indexOf('{', i), depth = 0;
  for(; j < src.length; j++){
    if(src[j] === '{') depth++;
    else if(src[j] === '}' && --depth === 0) break;
  }
  return src.slice(i, j + 1);
}
function extractConst(name){
  const m = src.match(new RegExp('(?:const|let)\\s+' + name + '\\s*=\\s*([^;]+);'));
  if(!m) throw new Error('constante absente : ' + name);
  return `const ${name} = ${m[1]};`;
}

const code = [
  extractConst('MAX_STEP_M'), extractConst('MAX_DV_KMH'),
  extractConst('splitSpeeds'), extractConst('splitDists'),
  'let anal = null; const scr = () => ({});',
  ...['distM','plausible','despike','despikeSpeed','makeLine','segCross',
      'crossingsOf','cumulDist','runSplits'].map(extractFn),
  'return {setAnal: a => { anal = a; }, getAnal: () => anal, distM, plausible, despike, despikeSpeed,',
  '        makeLine, crossingsOf, runSplits};',
].join('\n');
const C = new Function(code)();

const cases = JSON.parse(fs.readFileSync(casesPath, 'utf8'));
let fails = 0, checks = 0;
const cmp = (a, b, what) => {
  checks++;
  if(a == null || b == null || Math.abs(a - b) > 0.001){
    fails++; console.log(`  ECHEC ${what} : firmware ${a} / console ${b}`);
  }
};

for(const cs of cases){
  const raw = cs.pts.map(([t, lat, lon, speed, fix, sats]) =>
    ({itow: Math.round(t*1000), lat, lon, speed, fix, sats, alt: 200, ax: 0, ay: 0, az: 1}));
  // Chaîne identique à computeStats()
  const pts = C.despikeSpeed(C.despike(raw.filter(r => r.fix>=2 && Math.abs(r.lat)>0.0001 && C.plausible(r))));
  C.setAnal({pts});
  const idxOf = k => pts.findIndex(p => p.itow === raw[k].itow);
  const A = C.makeLine(idxOf(cs.idxA));
  const B = cs.idxB >= 0 ? C.makeLine(idxOf(cs.idxB)) : null;
  const fw = cs.events;

  if(!B){
    const cr = C.crossingsOf(A);
    const laps = []; for(let k=1;k<cr.length;k++) laps.push(cr[k].t - cr[k-1].t);
    const fwLaps = fw.filter(e => e.k === 1).map(e => e.t);
    checks++; if(laps.length !== fwLaps.length){ fails++; console.log(`  ECHEC ${cs.name} : ${fwLaps.length} tours firmware / ${laps.length} console`); }
    laps.forEach((l, i) => cmp(fwLaps[i], l, `${cs.name} tour ${i+1}`));
  }else{
    const ca = C.crossingsOf(A), cb = C.crossingsOf(B);
    let k = 0; const runs = [];
    for(const s of ca){
      while(k < cb.length && cb[k].t <= s.t) k++;
      if(k >= cb.length) break;
      runs.push({t: cb[k].t - s.t, splits: C.runSplits(s.i, cb[k].i, s.t)}); k++;
    }
    const fwRuns = fw.filter(e => e.k === 2).map(e => e.t);
    checks++; if(runs.length !== fwRuns.length){ fails++; console.log(`  ECHEC ${cs.name} : ${fwRuns.length} parcours / ${runs.length}`); }
    runs.forEach((r, i) => cmp(fwRuns[i], r.t, `${cs.name} parcours ${i+1}`));
    const fwSplits = fw.filter(e => e.k === 3);
    for(const sp of runs[0] ? runs[0].splits : []){
      const kind = sp.kind === 'v' ? 0 : 1;
      const val = parseFloat((sp.label.match(/([0-9.]+)\s*(?:km\/h|m)$/) || [])[1]);
      const f = fwSplits.find(e => e.sk === kind && Math.abs(e.sv - val) < 1e-6);
      cmp(f ? f.t : null, sp.t, `${cs.name} chrono ${sp.label}`);
    }
  }
}
console.log(`${'console'.padEnd(12)} ${checks - fails} OK, ${fails} ECHEC(S)`);
process.exit(fails ? 1 : 0);
