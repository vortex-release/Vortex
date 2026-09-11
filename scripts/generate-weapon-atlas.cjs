// Optional asset regeneration: Node.js + sharp. Normal C++ builds use the checked-in atlas.
const fs = require('fs');
const path = require('path');
const sharp = require('sharp');
async function main() {
  const root=path.resolve(__dirname,'..');
  const assets=path.join(root,'assets','weapons');
  const weapons=JSON.parse(fs.readFileSync(path.join(assets,'manifest.json'),'utf8').replace(/^\uFEFF/,''));
  const columns=8, cw=192, ch=72, width=columns*cw, height=Math.ceil(weapons.length/columns)*ch;
  const images=[];
  let header='#pragma once\n#include <cstdint>\nnamespace awareness {\nstruct WeaponIconInfo { std::uint32_t id; const char* name; float u0,v0,u1,v1; };\ninline constexpr WeaponIconInfo WeaponIcons[]{\n';
  const labels={deagle:'Desert Eagle',elite:'Dual Berettas',fiveseven:'Five-SeveN',glock:'Glock-18',ak47:'AK-47',aug:'AUG',awp:'AWP',famas:'FAMAS',g3sg1:'G3SG1',galilar:'Galil AR',m249:'M249',m4a1:'M4A4',mac10:'MAC-10',p90:'P90',mp5sd:'MP5-SD',ump45:'UMP-45',xm1014:'XM1014',bizon:'PP-Bizon',mag7:'MAG-7',negev:'Negev',sawedoff:'Sawed-Off',tec9:'Tec-9',hkp2000:'P2000',mp7:'MP7',mp9:'MP9',nova:'Nova',p250:'P250',scar20:'SCAR-20',sg556:'SG 553',ssg08:'SSG 08',m4a1_silencer:'M4A1-S',usp_silencer:'USP-S',cz75a:'CZ75-Auto',revolver:'R8 Revolver'};
  for(let i=0;i<weapons.length;i++) {
    const w=weapons[i], x=i%columns*cw, y=Math.floor(i/columns)*ch;
    const png=await sharp(path.join(assets,w.name+'.svg'),{density:192}).resize(cw-16,ch-16,{fit:'inside'}).png().toBuffer({resolveWithObject:true});
    images.push({input:png.data,left:x+Math.floor((cw-png.info.width)/2),top:y+Math.floor((ch-png.info.height)/2)});
    header+=`    {${w.id},"${labels[w.name]}",${(x/width).toFixed(8)}f,${(y/height).toFixed(8)}f,${((x+cw)/width).toFixed(8)}f,${((y+ch)/height).toFixed(8)}f},\n`;
  }
  header+='};\ninline const WeaponIconInfo* FindWeaponIcon(std::uint32_t id) noexcept { for(const auto& icon:WeaponIcons) if(icon.id==id)return &icon;return nullptr; }\n}\n';
  await sharp({create:{width,height,channels:4,background:{r:0,g:0,b:0,alpha:0}}}).composite(images).png().toFile(path.join(root,'assets','weapon-atlas.png'));
  fs.writeFileSync(path.join(root,'src','weapon_catalog.hpp'),header);
  console.log(`Generated ${weapons.length} firearm icons in ${width}x${height} atlas.`);
}
main().catch(e=>{console.error(e);process.exitCode=1;});
