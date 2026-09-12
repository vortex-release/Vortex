# Build checked-in ImGui vectors from pinned Lucide SVGs; no runtime parser.
import math,re,xml.etree.ElementTree as ET
from pathlib import Path
ROOT=Path(__file__).resolve().parents[1]
NAMES=['layout-dashboard','user-round','crosshair','route','globe','scan-line','sliders-horizontal','download','folder','circle-help','x','minus','chevron-right','search','check','settings-2','external-link','square','copy','monitor']
def arc(a,rx,ry,rot,large,sweep,b):
    rx,ry=abs(rx),abs(ry)
    if not rx or not ry or a==b:return [b]
    phi=math.radians(rot);c,s=math.cos(phi),math.sin(phi)
    dx,dy=(a[0]-b[0])/2,(a[1]-b[1])/2;x,y=c*dx+s*dy,-s*dx+c*dy
    k=x*x/(rx*rx)+y*y/(ry*ry)
    if k>1:rx*=math.sqrt(k);ry*=math.sqrt(k)
    q=math.sqrt(max(0,(rx*rx*ry*ry-rx*rx*y*y-ry*ry*x*x)/(rx*rx*y*y+ry*ry*x*x))) * (-1 if large==sweep else 1)
    cx,cy=q*rx*y/ry,-q*ry*x/rx
    mx,my=c*cx-s*cy+(a[0]+b[0])/2,s*cx+c*cy+(a[1]+b[1])/2
    theta=math.atan2((y-cy)/ry,(x-cx)/rx);end=math.atan2((-y-cy)/ry,(-x-cx)/rx)
    delta=(end-theta)%(2*math.pi)
    if not sweep:delta-=2*math.pi
    count=max(2,math.ceil(abs(delta)*8))
    return [(mx+c*rx*math.cos(theta+delta*i/count)-s*ry*math.sin(theta+delta*i/count),my+s*rx*math.cos(theta+delta*i/count)+c*ry*math.sin(theta+delta*i/count)) for i in range(1,count+1)]
def paths(d):
    tokens=re.findall(r'[A-Za-z]|[-+]?(?:\d*\.\d+|\d+\.?\d*)(?:[eE][-+]?\d+)?',d);i=0;cmd='';p=(0,0);start=p;line=[];result=[]
    while i<len(tokens):
        if tokens[i].isalpha():cmd=tokens[i];i+=1
        u=cmd.upper();rel=cmd.islower()
        if u=='Z':
            if line:result.append((line,True));line=[]
            p=start;cmd='';continue
        counts={'M':2,'L':2,'H':1,'V':1,'C':6,'Q':4,'A':7};n=counts[u];v=list(map(float,tokens[i:i+n]));i+=n
        def point(x,y):return (x+p[0],y+p[1]) if rel else (x,y)
        if u in ('M','L'):
            dest=point(*v)
            if u=='M':
                if line:result.append((line,False))
                line=[dest];start=dest;cmd='l' if rel else 'L'
            else:line.append(dest)
        elif u=='H':dest=(v[0]+p[0] if rel else v[0],p[1]);line.append(dest)
        elif u=='V':dest=(p[0],v[0]+p[1] if rel else v[0]);line.append(dest)
        elif u=='A':dest=point(v[5],v[6]);line+=arc(p,*v[:5],dest)
        elif u in ('C','Q'):
            pts=[p]+[point(v[j],v[j+1]) for j in range(0,n,2)];dest=pts[-1]
            for j in range(1,13):
                work=pts.copy();t=j/12
                while len(work)>1:work=[((1-t)*a[0]+t*b[0],(1-t)*a[1]+t*b[1]) for a,b in zip(work,work[1:])]
                line.append(work[0])
        p=dest
    if line:result.append((line,False))
    return result
out=['#pragma once','#include <imgui.h>','#include <cstdint>','#include <initializer_list>','// Lucide 0.468.0, ISC license. See assets/icons/lucide/LICENSE.','namespace vortex::icons {']
out+=['enum class Id : std::uint8_t { '+', '.join(x.title().replace('-','') for x in NAMES)+' };','struct Point { float x,y; };','struct Contour { unsigned first,count; bool closed; };','struct Shape { const Point* points; const Contour* contours; unsigned count; };']
shapes=[]
for name in NAMES:
    allpaths=[]
    for e in ET.parse(ROOT/'assets/icons/lucide'/f'{name}.svg').getroot():
        kind=e.tag.split('}')[-1];a=e.attrib
        if kind=='path':allpaths+=paths(a['d'])
        elif kind=='line':allpaths.append(([(float(a['x1']),float(a['y1'])),(float(a['x2']),float(a['y2']))],False))
        elif kind in ('polyline','polygon'):allpaths.append((list(zip(*[iter(map(float,re.findall(r'[-+\d.]+',a['points'])))]*2)),kind=='polygon'))
        elif kind=='circle':
            x,y,r=map(float,(a['cx'],a['cy'],a['r']));allpaths.append(([(x+r*math.cos(j*math.tau/48),y+r*math.sin(j*math.tau/48)) for j in range(48)],True))
        elif kind=='rect':
            x,y,w,h,r=map(float,(a.get('x',0),a.get('y',0),a['width'],a['height'],a.get('rx',0)))
            d=f'M{x+r} {y} H{x+w-r} A{r} {r} 0 0 1 {x+w} {y+r} V{y+h-r} A{r} {r} 0 0 1 {x+w-r} {y+h} H{x+r} A{r} {r} 0 0 1 {x} {y+h-r} V{y+r} A{r} {r} 0 0 1 {x+r} {y} Z'
            allpaths+=paths(d)
    ident=name.title().replace('-','');points=[];contours=[]
    for pts,closed in allpaths:contours.append((len(points),len(pts),closed));points+=pts
    num=lambda v:f'{v:.4f}f'
    out.append('inline constexpr Point '+ident+'Points[]{'+','.join('{'+num(x)+','+num(y)+'}' for x,y in points)+'};')
    out.append('inline constexpr Contour '+ident+'Contours[]{'+','.join('{'+str(a)+','+str(b)+','+str(c).lower()+'}' for a,b,c in contours)+'};')
    shapes.append('{'+ident+'Points,'+ident+'Contours,'+str(len(contours))+'}')
out.append('inline constexpr Shape Shapes[]{'+','.join(shapes)+'};')
out.append('''inline void Draw(ImDrawList* draw, Id id, ImVec2 at, float size, ImU32 color) {
    if (!draw || size <= 0) return;
    const auto index=static_cast<unsigned>(id);
    if(index >= sizeof(Shapes)/sizeof(Shapes[0])) return;
    const auto& shape=Shapes[index]; const float scale=size/24.f, width=2.f*scale;
    for(unsigned i=0;i<shape.count;++i) {
        const auto& contour=shape.contours[i];
        for(unsigned j=0;j<contour.count;++j) {
            const auto p=shape.points[contour.first+j];
            draw->PathLineTo({at.x+p.x*scale,at.y+p.y*scale});
        }
        draw->PathStroke(color,contour.closed?ImDrawFlags_Closed:ImDrawFlags_None,width);
        if(!contour.closed && contour.count) {
            for(unsigned j : {0u,contour.count-1}) {
                const auto p=shape.points[contour.first+j];
                draw->AddCircleFilled({at.x+p.x*scale,at.y+p.y*scale},width*.5f,color,8);
            }
        }
    }
}
} // namespace vortex::icons''')
(ROOT/'src/ui_icons.hpp').write_text('\n'.join(out)+'\n',encoding='utf-8')
