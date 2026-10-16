"""Read raw binary FBX units/vertices/transforms without application conversion."""
import sys,struct,zlib,json
from pathlib import Path
import numpy as np
def read(path):
 data=Path(path).read_bytes();version=struct.unpack_from('<I',data,23)[0];wide=version>=7500;header=25 if wide else 13
 def prop(pos):
  typ=chr(data[pos]);pos+=1
  if typ in 'YCLFDI':
   fmt={'Y':'h','C':'?','L':'q','F':'f','D':'d','I':'i'}[typ];value=struct.unpack_from('<'+fmt,data,pos)[0];return value,pos+struct.calcsize(fmt)
  if typ in 'SR':
   size=struct.unpack_from('<I',data,pos)[0];pos+=4;return data[pos:pos+size],pos+size
  count,encoding,size=struct.unpack_from('<III',data,pos);pos+=12;raw=data[pos:pos+size];raw=zlib.decompress(raw)if encoding else raw;dtype={'d':'<f8','f':'<f4','i':'<i4','l':'<i8','b':'u1','c':'u1'}[typ];return np.frombuffer(raw,dtype=dtype,count=count),pos+size
 def node(pos):
  end,count,propbytes,namelen=struct.unpack_from('<QQQB'if wide else'<IIIB',data,pos)
  if end==0:return None,pos+header
  pos+=header;name=data[pos:pos+namelen].decode();pos+=namelen;props=[]
  for _ in range(count):value,pos=prop(pos);props.append(value)
  children=[]
  while pos<end-header:
   child,pos=node(pos)
   if child is None:break
   children.append(child)
  return {'id':name,'props':props,'children':children},end
 roots=[];pos=27
 while pos<len(data)-header:
  item,pos=node(pos)
  if item is None:break
  roots.append(item)
 def strings(v):return v.decode(errors='replace')if isinstance(v,bytes)else v.tolist()if isinstance(v,np.ndarray)else v
 def properties(n):
  return {strings(p['props'][0]):[strings(x)for x in p['props'][4:]]for group in n['children']if group['id']=='Properties70'for p in group['children']if p['id']=='P'}
 report={'path':str(path),'version':version,'global_settings':{},'geometries':[],'models':[]}
 for root in roots:
  if root['id']=='GlobalSettings':report['global_settings']=properties(root)
  if root['id']=='Objects':
   for obj in root['children']:
    if obj['id']=='Geometry':
     for child in obj['children']:
      if child['id']=='Vertices':
       v=child['props'][0].reshape(-1,3);report['geometries'].append({'name':strings(obj['props'][1]),'vertices':len(v),'raw_min':v.min(0).tolist(),'raw_max':v.max(0).tolist()})
    elif obj['id']=='Model':report['models'].append({'name':strings(obj['props'][1]),'properties':properties(obj)})
 return report
if __name__=='__main__':
 reports=[read(p)for p in sys.argv[1:]];print(json.dumps(reports,indent=2))
