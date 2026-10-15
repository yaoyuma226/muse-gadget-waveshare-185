"""V9 stand-only revision. Approved V8 shell and speaker back are immutable.
mm; Python dependencies: cadquery, trimesh, manifold3d, scipy, numpy.
"""
from pathlib import Path
import math,json,hashlib,shutil
import cadquery as cq
import numpy as np,trimesh,manifold3d as M
from scipy.interpolate import PchipInterpolator
O=Path(__file__).parent
def box(x,y,z,p):return cq.Workplane('XY').box(x,y,z,centered=(True,True,False)).translate(p)
def mesh(o):
 v,f=o.val().tessellate(.045,.07)
 m=trimesh.Trimesh([[p.x,p.y,p.z] for p in v],f);m.fix_normals();return m
def union(ms):return trimesh.boolean.union(ms,engine='manifold')
def diff(a,b):return trimesh.boolean.difference([a,b],engine='manifold')
def clean(m):
 raw=M.Mesh(np.float32(m.vertices),np.uint32(m.faces),tolerance=.0001);raw.merge();r=M.Manifold(raw).simplify(.001).to_mesh()
 return trimesh.Trimesh(r.vert_properties[:,:3],r.tri_verts)
def overlap(a,b):
 m=trimesh.boolean.intersection([a,b],engine='manifold');return abs(float(m.volume)) if len(m.faces) else 0.
def rail(x,z=28,h=16,clear=.25):
 pts=[(x-2-clear,-2-clear),(x+2+clear,-2-clear),(x+4+clear,5+clear),(x-4-clear,5+clear)]
 return cq.Workplane('XY').workplane(offset=z).polyline(pts).close().extrude(h)
def world(m):
 m=m.copy();m.apply_transform(trimesh.transformations.rotation_matrix(math.pi,[0,1,0]));m.apply_transform(trimesh.transformations.rotation_matrix(math.radians(77),[1,0,0]));m.apply_translation([0,3-11.5*math.cos(math.radians(13)),65+11.5*math.sin(math.radians(13))]);return m
body=world(trimesh.load_mesh(O/'01_screen_shell.stl'))
back=trimesh.load_mesh(O/'02_twist_back.stl');back.apply_transform(trimesh.transformations.rotation_matrix(math.pi,[1,0,0]));back.apply_translation([0,0,31]);back=world(back)
# Closed, smooth loft, capped at35mm. No tall saddle fins or split base.
rows=np.array([(0,36,24,0),(.6,38,26,0),(1.8,40,28,0),(4.5,40,28,0),(6.2,38,26,0),(8,32,23,0),(12,25.5,22,.5),(18,22,21.5,1),(24,21,21.5,1),(29,21.5,21.5,1),(33,22,21.5,1),(34.5,21.8,21.3,1),(35,21.3,20.8,1)])
ip=PchipInterpolator(rows[:,0],rows[:,1:],axis=0);nu=192;nv=180;v=[];f=[]
for z in np.linspace(0,35,nv+1):
 rx,ry,cy=ip(z)
 for i in range(nu):
  a=2*math.pi*i/nu;v.append([rx*math.cos(a),cy+ry*math.sin(a),z])
for j in range(nv):
 for i in range(nu):
  a=j*nu+i;b=j*nu+(i+1)%nu;c=b+nu;d=a+nu;f.extend([[a,b,c],[a,c,d]])
for j in (0,nv):
 k=len(v);v.append(np.mean(v[j*nu:(j+1)*nu],axis=0))
 for i in range(nu):f.append([k,j*nu+(i+1)%nu,j*nu+i] if j==0 else [k,j*nu+i,j*nu+(i+1)%nu])
stand=trimesh.Trimesh(v,f);stand.fix_normals()
# A simple circular, swept seat cutter approximates the exterior with clearance.
# Use actual closed shell hull enlarged0.4mm for the tiny upper seat relief.
outer=cq.Workplane(obj=cq.Solid.makeLoft([cq.Workplane('XY').workplane(offset=z).circle(r).val() for r,z in [(29,0),(30,.8),(33,4),(35,10),(35,16),(34.3,22),(34.3,24)]]))
rear_outer=cq.Workplane(obj=cq.Solid.makeLoft([cq.Workplane('XY').workplane(offset=z).circle(r).val() for r,z in [(34.3,24.3),(34.15,25),(33.5,27),(31,30),(29.5,31)]]))
hull=union([world(mesh(cq.Workplane(obj=outer.val().scale(1.012)))),world(mesh(cq.Workplane(obj=rear_outer.val().scale(1.012))))])
rear_clear=back.convex_hull;rc=rear_clear.bounds.mean(axis=0);rear_clear.apply_translation(-rc);rear_clear.apply_scale(1.015);rear_clear.apply_translation(rc)
hull=union([hull,rear_clear])
cuts=[]
for lift in (0,1,2,3,4,5,7,10,15):
 h=hull.copy();h.apply_translation([0,0,lift]);cuts.append(h)
# Restore solid stop at28mm: only carve the seat above the top of the rail roots.
limit=mesh(box(100,100,40,(0,0,27.9)))
seat=trimesh.boolean.intersection([union(cuts),limit],engine='manifold')
cuts=[seat]
for x in (-12,12):cuts.append(mesh(rail(x)))
# Rounded internal cable cavity: deep enough for a straight plug, with a continuous front wall.
cuts.append(mesh(box(22,30,18,(0,-1,8)).edges('|Z').fillet(3)))
cuts.append(mesh(box(18,30,24,(0,-1,24)).edges('|Z').fillet(2)))
# Rounded rear exit,22wide x18high, opens onto the same internal cavity.
cuts.append(mesh(box(22,50,18,(0,25,3)).edges('|Y').fillet(4)))
stand=clean(diff(stand,union(cuts)))
assert stand.is_watertight and stand.is_winding_consistent and len(stand.split())==1
stand.export(O/'03_smooth_cradle_stand.stl')
stand=trimesh.load_mesh(O/'03_smooth_cradle_stand.stl');assert stand.is_watertight and len(stand.split())==1
print('Stand saved',stand.extents,flush=True)
checks={'single_connected_closed_mesh':True,'dimensions_mm':stand.extents.tolist(),'physical_fit_verified':False,'rail_clearance_per_side_mm':.25,'seat_height_mm':35,'stop_height_mm':28,'nominal_engagement_mm':7,'cable_rear_opening_mm':[22,18],'nominal_front_wall_at_centre_mm':4.5}
tests={}
for dz in (0,1,2,4,7,12,20):
 for n,p in [('shell',body),('back',back)]:
  m=p.copy();m.apply_translation([0,0,dz]);tests[f'{n}_lift_{dz}_overlap_mm3']=overlap(m,stand)
plug=world(mesh(box(14,24,8,(0,-38,3.8))))
tests['straight_plug_14x24x8_overlap_mm3']=overlap(plug,stand)
down=body.copy();down.apply_translation([0,0,-.5]);stop=overlap(down,stand)
checks['movement_checks']=tests;checks['stop_interference_when_pushed_down_0.5mm_mm3']=stop
checks['approved_part_sha256']={n:hashlib.sha256((O/n).read_bytes()).hexdigest() for n in ('01_screen_shell.stl','02_twist_back.stl')}
(O/'validation.json').write_text(json.dumps(checks,indent=2))
print(json.dumps(checks,indent=2),flush=True)
assert max(tests.values())<.02,tests
assert stop>1,'No positive seating stop'
# Optional inexpensive actual upper-neck test, with identical guide geometry.
coupon=trimesh.boolean.intersection([stand,mesh(box(100,100,12,(0,0,25)))],engine='manifold');coupon=clean(coupon);coupon.apply_translation([0,0,-25]);assert coupon.is_watertight and len(coupon.split())==1;coupon.export(O/'00_optional_docking_test.stl')
screen=world(mesh(cq.Workplane('XY').workplane(offset=.8).circle(27.5).extrude(.7)))
scene=trimesh.Scene()
for name,m,color in [('screen_shell',body,[233,227,209,255]),('speaker_back',back,[233,227,209,255]),('smooth_stand',stand,[126,148,128,255]),('screen_NOT_PRINT',screen,[10,14,12,255])]:
 m=m.copy();m.visual.vertex_colors=color;scene.add_geometry(m,node_name=name,geom_name=name)
scene.export(O/'Muse_V9_assembled.glb');print('V9 model exported',flush=True)
