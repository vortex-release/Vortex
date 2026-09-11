import sys,pathlib,struct,re
sys.path.insert(0,str(pathlib.Path.home()/"AppData/Local/Vortex/tools/analysis"))
import pefile,capstone
p=pefile.PE(r"C:\Program Files (x86)\Steam\steamapps\common\Counter-Strike Global Offensive\game\csgo\bin\win64\client.dll")
cs=capstone.Cs(capstone.CS_ARCH_X86,capstone.CS_MODE_64);base=p.OPTIONAL_HEADER.ImageBase;raw=p.__data__
target=int(sys.argv[1],16);refs=[]
for m in re.finditer(re.escape(struct.pack("<Q",base+target)),raw):
 r=p.get_rva_from_offset(m.start());refs.append(r);print("pointer",hex(r))
 for i in range(-4,5):
  value=struct.unpack("<Q",p.get_data(r+i*8,8))[0]-base
  print(i,hex(value))
sec=next(s for s in p.sections if s.Name.startswith(b".text"));code=sec.get_data()
for m in re.finditer(rb"\x48\x8d[\x05\x0d\x15\x1d\x35\x3d]",code):
 i=m.start();r=sec.VirtualAddress+i
 to=r+7+struct.unpack_from("<i",code,i+3)[0]
 if not any(x-32<=to<=x for x in refs):continue
 f=next((e.struct for e in p.DIRECTORY_ENTRY_EXCEPTION if e.struct.BeginAddress<=r<e.struct.EndAddress),None)
 if not f:continue
 print("xref",hex(r),"to",hex(to),"fn",hex(f.BeginAddress))
 ins=list(cs.disasm(p.get_data(f.BeginAddress,f.EndAddress-f.BeginAddress),f.BeginAddress))
 idx=next((n for n,x in enumerate(ins) if x.address==r),0)
 for x in ins[max(0,idx-10):idx+9]:print(hex(x.address),x.mnemonic,x.op_str)
