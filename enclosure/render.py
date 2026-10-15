from pathlib import Path
import numpy as np,trimesh,vtk
from vtk.util.numpy_support import numpy_to_vtk,numpy_to_vtkIdTypeArray
from PIL import Image,ImageDraw,ImageFont
O=Path(__file__).parent
s=trimesh.load(O/'Muse_V9_assembled.glb')
w=vtk.vtkRenderWindow();w.SetOffScreenRendering(1);w.SetSize(1600,800);w.SetMultiSamples(8)
for i,pos in enumerate([(100,-195,110),(-100,195,110)]):
 r=vtk.vtkRenderer();r.SetViewport(i*.5,0,(i+1)*.5,1);r.SetBackground(.953,.953,.937);w.AddRenderer(r)
 for n,m in s.geometry.items():
  if n.startswith('speaker_') and 'back' not in n:continue
  p=vtk.vtkPoints();p.SetData(numpy_to_vtk(np.ascontiguousarray(m.vertices),deep=True))
  c=vtk.vtkCellArray();c.SetCells(len(m.faces),numpy_to_vtkIdTypeArray(np.c_[np.full(len(m.faces),3),m.faces].astype(np.int64).ravel(),deep=True))
  data=vtk.vtkPolyData();data.SetPoints(p);data.SetPolys(c)
  norms=vtk.vtkPolyDataNormals();norms.SetInputData(data);norms.SetFeatureAngle(45);norms.SplittingOn();norms.Update()
  mp=vtk.vtkPolyDataMapper();mp.SetInputConnection(norms.GetOutputPort());a=vtk.vtkActor();a.SetMapper(mp)
  a.GetProperty().SetColor(*((.48,.56,.47) if 'stand' in n else (.025,.035,.03) if 'NOT_PRINT' in n else (.91,.88,.81)))
  a.GetProperty().SetInterpolationToPhong();
  if 'speaker_back' in n:a.GetProperty().SetInterpolationToFlat()
  a.GetProperty().SetAmbient(.3);a.GetProperty().SetDiffuse(.7);a.GetProperty().SetSpecular(.05);a.GetProperty().SetSpecularPower(25);r.AddActor(a)
 cam=r.GetActiveCamera();cam.SetPosition(*pos);cam.SetFocalPoint(0,0,51);cam.SetViewUp(0,0,1);cam.ParallelProjectionOn();cam.SetParallelScale(64);r.ResetCameraClippingRange()
w.Render();cap=vtk.vtkWindowToImageFilter();cap.SetInput(w);cap.Update();writer=vtk.vtkPNGWriter();writer.SetFileName(str(O/'Muse_V9_preview.png'));writer.SetInputConnection(cap.GetOutputPort());writer.Write()
im=Image.open(O/'Muse_V9_preview.png').convert('RGB');d=ImageDraw.Draw(im)
font='/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf'
for text,y,size in [('MUSE V9 · smooth cradle stand',20,30),('New one-piece cradle · rounded rear cable opening · lift-off puck',760,22)]:
 f=ImageFont.truetype(font,size);d.text((800,y),text,font=f,fill='#27392f',anchor='mt')
im.save(O/'Muse_V9_preview.png')
print('Rendered CAD geometry')
