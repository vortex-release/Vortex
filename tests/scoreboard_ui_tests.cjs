const fs = require('node:fs');
const path = require('node:path');
const assert = require('node:assert/strict');
const vm = require('node:vm');
const text = fs.readFileSync(path.join(__dirname, '../src/scoreboard_script.hpp'), 'utf8');
const parts = [...text.matchAll(/R"VORTEX\(([\s\S]*?)\)VORTEX"/g)].map(m=>m[1]);
assert.equal(parts.length,2);
let created=0, imageLoads=0;
class Panel {
 constructor(id,parent=null){this.id=id;this.parent=parent;this.children=[];this.style={};this.attrs={};this.classes=new Set();this.visible=true;this.valid=true;if(parent)parent.children.push(this);}
 IsValid(){return this.valid;}
 GetParent(){return this.parent;}
 FindChildTraverse(id){for(const p of this.children){if(p.id===id)return p;const f=p.FindChildTraverse(id);if(f)return f;}return null;}
 FindChildrenWithClassTraverse(c){return this.children.flatMap(p=>[...(p.classes.has(c)?[p]:[]),...p.FindChildrenWithClassTraverse(c)]);}
 AddClass(c){this.classes.add(c);}
 GetAttributeString(k,d){return this.attrs[k]??d;}
 SetAttributeString(k,v){this.attrs[k]=v;}
 Children(){return this.children.slice();}
 SetImage(p){imageLoads++;this.image=p;}
}
let root = new Panel('root');
function board(){const b=new Panel('Scoreboard',root);const r=new Panel('player-76561198012345678',b);const host=new Panel('id-sb-name__nameicons',r);const original=new Panel('nativeRank',host);return {b,host,original};}
let ui=board();
const context=vm.createContext({$: {GetContextPanel:()=>root,CreatePanel:(kind,parent,id)=>{created++;return new Panel(id,parent);}},GameStateAPI:{GetPlayerXuidStringFromPlayerSlot:slot=>String(slot)}});
const render=(rows,scale=1)=>vm.runInContext(parts[0]+JSON.stringify(rows)+';var scale='+scale+parts[1],context);
const row=['76561198012345678',1,[['ak47',true,false],['defuser',false,true]],524289];
render([row]);let box=ui.host.FindChildTraverse('VortexEquipmentV1_1_524289_76561198012345678');
assert.ok(box);assert.equal(created,3);assert.equal(imageLoads,2);assert.equal(ui.original.visible,true);
assert.equal(box.children[0].image,'file://{images}/icons/equipment/ak47.svg');
render([row]);assert.equal(created,3);assert.equal(imageLoads,2);
row[2]=[['awp',true,false]];render([row],1.25);assert.equal(created,3);assert.equal(imageLoads,3);assert.equal(box.children[1].visible,false);assert.equal(box.children[0].style.width,'46px');
render([]);assert.equal(box.visible,false);assert.equal(ui.original.visible,true);
render([row]);assert.equal(box.visible,true);assert.equal(created,3);
// A reused controller slot must not keep the previous player's box visible.
const otherRow=new Panel('player-76561198099999999',ui.b);
const otherHost=new Panel('id-sb-name__nameicons',otherRow);
render([['76561198099999999',1,[['awp',true,false]],1048577]]);
assert.equal(box.visible,false);assert.equal(otherHost.children.length,1);assert.equal(otherHost.children[0].visible,true);
// Bots use a zero-based player slot string, including the valid first slot '0'.
const botRow=new Panel('player-0',ui.b);
const botHost=new Panel('id-sb-name__nameicons',botRow);
render([['0',1,[['glock',true,false]],1572865]]);
assert.equal(botHost.children.length,1);assert.equal(botHost.children[0].visible,true);
assert.equal(otherHost.children[0].visible,false);
root=new Panel('replacement');ui=board();render([row]);assert.ok(ui.host.FindChildTraverse('VortexEquipmentV1_1_524289_76561198012345678'));assert.equal(ui.original.visible,true);
// Missing rows are normal during scoreboard rebuild. Never create a floating detached widget.
root=new Panel('empty');const before=created;render([row]);assert.equal(created,before);
console.log('Scoreboard UI: reuse, asset diffing, precision, bot slots, cleanup, and tree rebuild checks passed.');
