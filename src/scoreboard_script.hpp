#pragma once
namespace awareness::scoreboard {
inline constexpr char ScriptPrefix[] = R"VORTEX((function(){try{
var root=$.GetContextPanel();if(!root||!root.IsValid())return;
while(root.GetParent())root=root.GetParent();
var board=root.FindChildTraverse('Scoreboard');if(!board||!board.IsValid())return;
var rows=)VORTEX";
inline constexpr char ScriptBody[] = R"VORTEX(;
var owner='VortexEquipmentV1';
var current={};
function valid(p){return p&&p.IsValid();}
function rowFor(p){
 var ids=[];if(p[0]&&p[0]!=='0')ids.push(p[0]);
 if(typeof GameStateAPI!=='undefined'&&GameStateAPI.GetPlayerXuidStringFromPlayerSlot){
  var x=GameStateAPI.GetPlayerXuidStringFromPlayerSlot(p[1]-1);if(x!==undefined&&x!==null&&String(x)!=='')ids.push(String(x));
 }
 var prefixes=['player-','id-','id-player-','player_'];
 for(var i=0;i<ids.length;i++){
  if(!ids[i])continue;
  for(var j=0;j<prefixes.length;j++){var r=board.FindChildTraverse(prefixes[j]+ids[i]);if(valid(r))return r;}
 }
 return null;
}
for(var i=0;i<rows.length;i++){
 var p=rows[i],row=rowFor(p);if(!row)continue;
 var host=row.FindChildTraverse('id-sb-name__nameicons');if(!valid(host))continue;
 var id=owner+'_'+p[1]+'_'+p[3]+'_'+p[0],box=host.FindChildTraverse(id);
 if(!valid(box)){box=$.CreatePanel('Panel',host,id);box.AddClass(owner);box.style.flowChildren='right';box.style.verticalAlign='center';box.style.marginLeft='6px';box.style.width='fit-children';}
 current[id]=true;box.visible=true;box.style.height=Math.round(22*scale)+'px';
 var signature=JSON.stringify([p[2],scale]);
 if(box.GetAttributeString('vortexSignature','')===signature)continue;
 box.SetAttributeString('vortexSignature',signature);
 var children=box.Children();
 for(var k=0;k<children.length;k++)children[k].visible=false;
 for(var k=0;k<p[2].length;k++){
  var item=p[2][k],iid=id+'_'+k,im=box.FindChildTraverse(iid);
  if(!valid(im)){im=$.CreatePanel('Image',box,iid);im.style.verticalAlign='center';im.style.margin='0px 3px';im.style.imgShadow='0px 1px 2px #000000aa';im.style.washColor='#e6e7ed';}
  im.visible=true;im.style.width=Math.round((item[2]?19:37)*scale)+'px';im.style.height=Math.round(20*scale)+'px';im.style.opacity=item[1]?'1':'0.48';
  if(im.GetAttributeString('vortexAsset','')!==item[0]){im.SetImage('file://{images}/icons/equipment/'+item[0]+'.svg');im.SetAttributeString('vortexAsset',item[0]);}
 }
}
var owned=board.FindChildrenWithClassTraverse(owner);
for(var i=0;i<owned.length;i++)if(valid(owned[i])&&!current[owned[i].id])owned[i].visible=false;
}catch(e){}})();)VORTEX";
} // namespace awareness::scoreboard
