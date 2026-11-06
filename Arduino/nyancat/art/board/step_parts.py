import sys
from OCP.STEPCAFControl import STEPCAFControl_Reader
from OCP.TDocStd import TDocStd_Document
from OCP.TCollection import TCollection_ExtendedString
from OCP.XCAFDoc import XCAFDoc_DocumentTool
from OCP.TDF import TDF_Label, TDF_ChildIterator
from OCP.TDataStd import TDataStd_Name
from OCP.Bnd import Bnd_Box
from OCP.BRepBndLib import BRepBndLib
from OCP.TopLoc import TopLoc_Location
doc = TDocStd_Document(TCollection_ExtendedString("doc"))
r = STEPCAFControl_Reader(); r.SetNameMode(True)
r.ReadFile("board/2d3d/ESP32-S3-Touch-LCD-1_47_20250411.stp"); r.Transfer(doc)
st = XCAFDoc_DocumentTool.ShapeTool_s(doc.Main())
def name(l):
    a = TDataStd_Name()
    return a.Get().ToExtString() if l.FindAttribute(TDataStd_Name.GetID_s(), a) else "?"
def bbox(s):
    b = Bnd_Box(); BRepBndLib.Add_s(s, b, True)
    p,q=b.CornerMin(),b.CornerMax(); return p.X(),p.Y(),p.Z(),q.X(),q.Y(),q.Z()
out=[]
def walk(l, loc, depth, path):
    if st.IsReference_s(l):
        ref = TDF_Label(); st.GetReferredShape_s(l, ref)
        loc = loc.Multiplied(st.GetLocation_s(l)); nm = name(l); l2 = ref
    else:
        nm = name(l); l2 = l
    if st.IsAssembly_s(l2) and depth < 6 and not any(k in nm for k in ('PIN_HEADER','HEX_STUD','HSX','BOX')):
        it = TDF_ChildIterator(l2, False)
        while it.More():
            c = it.Value()
            if st.IsComponent_s(c): walk(c, loc, depth+1, path+[nm])
            it.Next()
    else:
        s = st.GetShape_s(l2).Moved(loc)
        x0,y0,z0,x1,y1,z1 = bbox(s)
        out.append((nm, x0,y0,z0,x1,y1,z1))
it = TDF_ChildIterator(st.Label(), False)
while it.More():
    l = it.Value()
    if st.IsFree_s(l) and st.IsShape_s(l): walk(l, TopLoc_Location(), 0, [])
    it.Next()
for o in sorted(out, key=lambda o:-(o[4]-o[1])*(o[5]-o[2])):
    n,x0,y0,z0,x1,y1,z1=o
    print("%-44s x[%7.2f %7.2f] y[%7.2f %7.2f] z[%7.2f %7.2f]" % (n[:44],x0,x1,y0,y1,z0,z1))
