from pathlib import Path
import json,zipfile,shutil
import numpy as np
import trimesh
from xml.etree.ElementTree import Element,SubElement,tostring,register_namespace
ROOT=Path(__file__).parent
NS='http://schemas.microsoft.com/3dmanufacturing/core/2015/02'
register_namespace('',NS)
def node(parent,tag,attrs=None):return SubElement(parent,'{'+NS+'}'+tag,attrs or {})
mesh_cache={}
checks={}
for p in sorted(ROOT.glob('*.stl')):
    if '_inspect' in p.name:continue
    m=trimesh.load_mesh(p,process=True)
    if isinstance(m,trimesh.Scene):m=m.to_geometry()
    assert m.is_watertight,(p.name,'not watertight')
    assert m.is_winding_consistent,(p.name,'inconsistent winding')
    assert len(m.split(only_watertight=False))==1,(p.name,'disconnected mesh')
    assert m.volume>0,(p.name,'inverted')
    mesh_cache[p.stem]=m
    checks[p.stem]={'watertight':True,'consistent_winding':True,'connected_components':1,'triangles':len(m.faces),'volume_mm3':round(float(m.volume),2)}
def project(filename,items):
    root=Element('{'+NS+'}model',{'unit':'millimeter','{http://www.w3.org/XML/1998/namespace}lang':'en-US'})
    node(root,'metadata',{'name':'Title'}).text=filename
    node(root,'metadata',{'name':'Designer'}).text='Muse V9 — PLA mechanical prototype'
    resources=node(root,'resources');materials=node(resources,'basematerials',{'id':'1'})
    for name,color in [('Ivory','#E8E4D9FF'),('Sage','#899487FF'),('Black','#242824FF')]:node(materials,'base',{'name':name,'displaycolor':color})
    build=node(root,'build');ids={};placement=[]
    for name,x,y in items:
        m=mesh_cache[name]
        if name not in ids:
            oid=len(ids)+2;ids[name]=oid
            mat='1' if 'stand' in name or 'cover' in name else '2' if any(s in name for s in ['adapter','retaining','button']) else '0'
            obj=node(resources,'object',{'id':str(oid),'type':'model','name':name,'pid':'1','pindex':mat})
            mesh=node(obj,'mesh');vertices=node(mesh,'vertices');triangles=node(mesh,'triangles')
            for v in m.vertices:node(vertices,'vertex',dict(zip(['x','y','z'],[f'{q:.6f}' for q in v])))
            for f in m.faces:node(triangles,'triangle',dict(zip(['v1','v2','v3'],[str(q) for q in f])))
        bounds=m.bounds;tx=x-float(bounds[:,0].mean());ty=y-float(bounds[:,1].mean());tz=-float(bounds[0,2])
        lo=bounds[0]+[tx,ty,tz];hi=bounds[1]+[tx,ty,tz]
        assert min(lo[:2])>=3 and max(hi[:2])<=177,(name,'plate edge',lo,hi)
        for other,ol,oh in placement:
            overlap=np.minimum(hi[:2],oh[:2])-np.maximum(lo[:2],ol[:2])
            assert not np.all(overlap>0),(name,other,'overlapping layout')
        placement.append((name,lo,hi))
        node(build,'item',{'objectid':str(ids[name]),'transform':f'1 0 0 0 1 0 0 0 1 {tx:.6f} {ty:.6f} {tz:.6f}'})
    content=tostring(root,encoding='utf-8',xml_declaration=True)
    with zipfile.ZipFile(ROOT/filename,'w',zipfile.ZIP_DEFLATED) as z:
        z.writestr('[Content_Types].xml','<?xml version="1.0" encoding="UTF-8"?><Types xmlns="http://schemas.openxmlformats.org/package/2006/content-types"><Default Extension="rels" ContentType="application/vnd.openxmlformats-package.relationships+xml"/><Default Extension="model" ContentType="application/vnd.ms-package.3dmanufacturing-3dmodel+xml"/></Types>')
        z.writestr('_rels/.rels','<?xml version="1.0" encoding="UTF-8"?><Relationships xmlns="http://schemas.openxmlformats.org/package/2006/relationships"><Relationship Target="/3D/3dmodel.model" Id="rel0" Type="http://schemas.microsoft.com/3dmanufacturing/2013/01/3dmodel"/></Relationships>')
        z.writestr('3D/3dmodel.model',content)
    print('3MF',filename,len(items),'objects')


project('Muse_V9_Bambu_layout.3mf',[('01_screen_shell',44,44),('02_twist_back',130,44),('03_smooth_cradle_stand',86,130)])
(ROOT/'mesh_checks.json').write_text(json.dumps(checks,indent=2))
dest=ROOT.parent/'Muse_V9_Print_Package.zip'
with zipfile.ZipFile(dest,'w',zipfile.ZIP_DEFLATED) as z:
    for pattern in ('*.stl','*.3mf','*.md','*.json','*.html','*.glb','*.png','*.py'):
        for p in sorted(ROOT.glob(pattern)):z.write(p,p.name)
print(dest,dest.stat().st_size)
