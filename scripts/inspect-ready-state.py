import sys,pathlib,struct,re
sys.path.insert(0,str(pathlib.Path.home()/"AppData/Local/Vortex/tools/analysis"))
import pefile,capstone
p=pefile.PE(r"C:\Program Files (x86)\Steam\steamapps\common\Counter-Strike Global Offensive\game\csgo\bin\win64\client.dll")
cs=capstone.Cs(capstone.CS_ARCH_X86,capstone.CS_MODE_64)
for ins in cs.disasm(p.get_data(0xcadda0,500),0xcadda0):
 if ins.mnemonic=="lea" and "rip" in ins.op_str:
  b=ins.bytes; dest=ins.address+ins.size+struct.unpack("<i",b[-4:])[0]
  print(hex(ins.address),hex(dest),repr(p.get_data(dest,90).split(b"\0")[0]))
sec=next(s for s in p.sections if s.Name.startswith(b".text"));code=sec.get_data()
for target in (0xf3e810,):
 for m in re.finditer(rb"\xe8",code):
  i=m.start();rva=sec.VirtualAddress+i
  if i+5<=len(code) and rva+5+struct.unpack_from("<i",code,i+1)[0]==target:
   fn=next((e.struct for e in p.DIRECTORY_ENTRY_EXCEPTION if e.struct.BeginAddress<=rva<e.struct.EndAddress),None)
   if fn:
    ins=list(cs.disasm(p.get_data(fn.BeginAddress,fn.EndAddress-fn.BeginAddress),fn.BeginAddress))
    idx=next((n for n,x in enumerate(ins) if x.address==rva),0)
    print("call",hex(rva))
    for x in ins[max(0,idx-7):idx+2]:print(hex(x.address),x.mnemonic,x.op_str)
