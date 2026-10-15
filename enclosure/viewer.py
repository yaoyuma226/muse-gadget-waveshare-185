from pathlib import Path
import numpy as np,trimesh
import matplotlib
matplotlib.use('Agg')
import matplotlib.pyplot as plt
from mpl_toolkits.mplot3d.art3d import Poly3DCollection
import plotly.graph_objects as go
O=Path(__file__).parent
s=trimesh.load(O/'Muse_V9_assembled.glb')
parts={n:s.geometry[n].copy() for n in s.geometry}
colors={n:('#83947e' if 'stand' in n else '#141a18' if 'NOT_PRINT' in n else '#e9e3d1') for n in parts}
fig=go.Figure()
for n,m in parts.items():
 if n.startswith('speaker'):continue
 v=m.vertices;f=m.faces
 fig.add_trace(go.Mesh3d(x=v[:,0],y=v[:,1],z=v[:,2],i=f[:,0],j=f[:,1],k=f[:,2],color=colors[n],name=n.replace('_',' '),flatshading=False,lighting=dict(ambient=.6,diffuse=.75,specular=.15,roughness=.8),hoverinfo='name'))
fig.update_layout(title='Muse V9 — drag to rotate · scroll to zoom',paper_bgcolor='#f3f3ef',margin=dict(l=0,r=0,t=60,b=0),scene=dict(aspectmode='data',xaxis=dict(visible=False),yaxis=dict(visible=False),zaxis=dict(visible=False),camera=dict(eye=dict(x=1.4,y=-2,z=1.1))),showlegend=True)
fig.write_html(O/'Muse_V9_360.html',include_plotlyjs=True,full_html=True)
print('Preview and offline 360 viewer created')
