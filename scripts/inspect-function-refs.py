import sys,pathlib,struct,re
sys.path.insert(0,str(pathlib.Path.home()/"AppData/Local/Vortex/tools/analysis"))
import pefile,capstone
p=pefile.PE(r"C:\Program Files (x86)\Steam\steamapps\common\Counter-Strike Global Offensive\game\csgo\bin\win64\client.dll")
cs=capstone.Cs(capstone.CS_ARCH_X86,capstone.CS_MODE_64)
at=int(sys.argv[1],16)
fn=next((e.struct for e in p.DIRECTORY_ENTRY_EXCEPTION if e.struct.BeginAddress<=at<e.struct.EndAddress),None)
if fn:
 print("fn",hex(fn.BeginAddress),hex(fn.EndAddress))
 for x in cs.disasm(p.get_data(fn.BeginAddress,90),fn.BeginAddress):print(hex(x.address),x.mnemonic,x.op_str)
 target=fn.BeginAddress
else:target=at
sec=next(s for s in p.sections if s.Name.startswith(b".text"));code=sec.get_data()
for m in re.finditer(rb"\xe8|\x48\x8d[\x05\x0d\x15\x1d\x35\x3d]",code):
 i=m.start();rva=sec.VirtualAddress+i;n=5 if code[i]==0xe8 else 7
 if i+n<=len(code) and rva+n+struct.unpack_from("<i",code,i+n-4)[0]==target:
  f=next((e.struct for e in p.DIRECTORY_ENTRY_EXCEPTION if e.struct.BeginAddress<=rva<e.struct.EndAddress),None)
  if f:
   ins=list(cs.disasm(p.get_data(f.BeginAddress,f.EndAddress-f.BeginAddress),f.BeginAddress))
   idx=next((n for n,x in enumerate(ins) if x.address==rva),0)
   print("xref",hex(rva))
   for x in ins[max(0,idx-9):idx+7]:print(hex(x.address),x.mnemonic,x.op_str)
