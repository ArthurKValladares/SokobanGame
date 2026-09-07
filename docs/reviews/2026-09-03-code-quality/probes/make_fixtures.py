from pathlib import Path
import struct,json,math,sys
p=Path(sys.argv[1]);p.mkdir(parents=True,exist_ok=True)
v=[0,2,0,4,0,0,0,2,1]+[1/math.sqrt(5),2/math.sqrt(5),0]*3
b=struct.pack('<18f',*v)+struct.pack('<6f',0,0,1,0,0,1)+struct.pack('<3H',0,1,2)
(p/'triangle.bin').write_bytes(b)
j={'asset':{'version':'2.0'},'buffers':[{'uri':'triangle.bin','byteLength':len(b)}],
   'bufferViews':[{'buffer':0,'byteOffset':0,'byteLength':36},{'buffer':0,'byteOffset':36,'byteLength':36},{'buffer':0,'byteOffset':72,'byteLength':24},{'buffer':0,'byteOffset':96,'byteLength':6}],
   'accessors':[{'bufferView':0,'componentType':5126,'count':3,'type':'VEC3','min':[0,0,0],'max':[4,2,1]},
                {'bufferView':1,'componentType':5126,'count':3,'type':'VEC3'},
                {'bufferView':2,'componentType':5126,'count':3,'type':'VEC2'},
                {'bufferView':3,'componentType':5123,'count':3,'type':'SCALAR'}],
   'meshes':[{'primitives':[{'attributes':{'POSITION':0,'NORMAL':1,'TEXCOORD_0':2},'indices':3}]}]}
(p/'nonuniform.gltf').write_text(json.dumps(j))
data=json.dumps(j).encode();data+=b' '*((-len(data))%4)
(p/'external.glb').write_bytes(struct.pack('<III',0x46546c67,2,20+len(data))+struct.pack('<II',len(data),0x4e4f534a)+data)
