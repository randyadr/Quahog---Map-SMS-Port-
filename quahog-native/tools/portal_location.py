"""Locate the gap between the coconut and papaya market stalls from the user's disc.
No retail stage data is shipped. Returns None when anchors/wall are ambiguous.
"""
import math,struct

def sub(a,b):return tuple(x-y for x,y in zip(a,b))
def dot(a,b):return sum(x*y for x,y in zip(a,b))
def cross(a,b):return (a[1]*b[2]-a[2]*b[1],a[2]*b[0]-a[0]*b[2],a[0]*b[1]-a[1]*b[0])
def ray(o,d,t):
    a,b,c=t;e1=sub(b,a);e2=sub(c,a);h=cross(d,e2);det=dot(e1,h)
    if abs(det)<1e-6:return None
    s=sub(o,a);u=dot(s,h)/det
    if not 0<=u<=1:return None
    q=cross(s,e1);v=dot(d,q)/det
    if v<0 or u+v>1:return None
    dist=dot(e2,q)/det
    return dist if dist>0 else None

def triangles(data):
    if len(data)<16:raise ValueError('Short COL')
    nv,vo,ng,go=struct.unpack_from('>4I',data)
    if nv>1000000 or ng>100000 or vo+nv*12>len(data) or go+ng*24>len(data):raise ValueError('COL header bounds')
    vertices=list(struct.iter_unpack('>3f',data[vo:vo+nv*12]))
    for i in range(ng):
        _,n,_,_,offset,_,_,_=struct.unpack_from('>HhHH4I',data,go+i*24)
        if n<0 or offset+n*6>len(data):raise ValueError('COL faces bounds')
        for face in struct.iter_unpack('>3h',data[offset:offset+n*6]):
            if min(face)<0 or max(face)>=nv:raise ValueError('COL vertex index')
            yield tuple(vertices[j] for j in face)

def market(scene,col):
    fruits={'coconut':[],'papaya':[]}
    for n in scene.walk():
        if n.type not in ('Fruit','ResetFruit','MapObjBase','MapObjGeneral'):continue
        if len(n.payload)<36:continue
        for kind in fruits:
            if ('Fruit'+kind).lower().encode() in n.payload.lower():
                p=struct.unpack_from('>3f',n.payload)
                if all(math.isfinite(x) and abs(x)<50000 for x in p):fruits[kind].append(p)
    # The nearest pair identifies the open gap between the two stalls,
    # rather than their centres (which would put the portal behind a canopy).
    candidates=sorted((math.dist(a,b),a,b) for a in fruits['coconut'] for b in fruits['papaya'] if abs(a[1]-b[1])<100)
    if not candidates or not 150<candidates[0][0]<2200:return None
    distance,a,b=candidates[0];mid=tuple((x+y)*.5 for x,y in zip(a,b));along=sub(b,a);length=math.hypot(along[0],along[2])
    if length<1:return None
    direction=(-along[2]/length,0,along[0]/length);origin=(mid[0],mid[1]+190,mid[2]);faces=list(triangles(col));hits=[]
    for sign in (1,-1):
        d=tuple(x*sign for x in direction)
        for t in faces:
            n=cross(sub(t[1],t[0]),sub(t[2],t[0]));norm=math.sqrt(dot(n,n))
            if norm<60000 or abs(n[1])>norm*.3:continue
            h=ray(origin,d,t)
            if h is not None and 80<h<1100:hits.append((h,d))
    if not hits:return None
    h,d=min(hits,key=lambda x:x[0]);p=(origin[0]+d[0]*(h-75),origin[1],origin[2]+d[2]*(h-75))
    floor=[v for t in faces if (v:=ray((p[0],mid[1]+120,p[2]),(0,-1,0),t)) is not None and v<350]
    if not floor:return None
    y=mid[1]+120-min(floor)
    return {'position':[p[0],y,p[2]],'yaw':math.degrees(math.atan2(-d[0],-d[2])),
            'method':'coconut/papaya market gap + nearest large wall collision','anchor_gap':distance}
