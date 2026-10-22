import fs from 'node:fs';
const p=JSON.parse(fs.readFileSync('D:/work/AI/yolo-study-001-clean/runs/desktop/profile-backups/p_698dccab94ce4e2f854389105932572e-20261022-100004-070695.json','utf8'));
let text='# Offline copy of gamepad settings only. No device outputs.\n';
function table(path,value) {
 text+=`\n[${path}]\n`;
 for(const [k,v] of Object.entries(value))if(v!==null&&typeof v!=='object')text+=`${k} = ${JSON.stringify(v)}\n`;
 for(const [k,v] of Object.entries(value))if(v&&typeof v==='object'&&!Array.isArray(v))table(path+'.'+k,v);
}
table('gamepad',p.config.gamepad);
// Same curve algorithm used by this clean profile; user curves are not changed.
text+='\n[gamepad.aim_response_curve]\nalgorithm = "cod_dynamic_legacy_lut"\n';
fs.writeFileSync('runs/downpull-20261023/profile.toml',text);
console.log('prepared profile; curve metadata:',JSON.stringify(p.curve));
