exec(open('board/step_parts.py').read().split("out=[]")[0])
from OCP.TopExp import TopExp_Explorer
from OCP.TopAbs import TopAbs_SOLID
res=[]
def walk(l, loc, path):
    if st.IsReference_s(l):
        ref = TDF_Label(); st.GetReferredShape_s(l, ref); loc = loc.Multiplied(st.GetLocation_s(l)); l = ref
    nm = name(l)
    if 'SWITCHER' in nm or 'SWITCHER' in ''.join(path):
        s = st.GetShape_s(l).Moved(loc)
        e = TopExp_Explorer(s, TopAbs_SOLID)
        while e.More():
            res.append((nm, bbox(e.Current()))); e.Next()
        return
    if st.IsAssembly_s(l):
        it = TDF_ChildIterator(l, False)
        while it.More():
            c = it.Value()
            if st.IsComponent_s(c): walk(c, loc, path+[nm])
            it.Next()
it = TDF_ChildIterator(st.Label(), False)
while it.More():
    l = it.Value()
    if st.IsFree_s(l) and st.IsShape_s(l): walk(l, TopLoc_Location(), [])
    it.Next()
for n,b in res:
    print("%-30s x[%6.2f %6.2f] y[%6.2f %6.2f] z[%6.2f %6.2f]"%(n[:30],b[0],b[3],b[1],b[4],b[2],b[5]))

from OCP.BRepMesh import BRepMesh_IncrementalMesh
from OCP.BRep import BRep_Tool
from OCP.TopAbs import TopAbs_FACE
from OCP.TopoDS import TopoDS
import numpy as np
def pts(shape):
    BRepMesh_IncrementalMesh(shape, 0.02, False, 0.2, True)
    P=[]
    e=TopExp_Explorer(shape, TopAbs_FACE)
    while e.More():
        f=TopoDS.Face(e.Current()); loc=TopLoc_Location()
        t=BRep_Tool.Triangulation_s(f, loc)
        if t is not None:
            tr=loc.Transformation()
            for i in range(1,t.NbNodes()+1):
                p=t.Node(i).Transformed(tr); P.append((p.X(),p.Y(),p.Z()))
        e.Next()
    return np.array(P)
