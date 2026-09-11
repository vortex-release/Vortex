import sys,pathlib,struct,re
sys.path.insert(0,str(pathlib.Path.home()/"AppData/Local/Vortex/tools/analysis"))
import pefile,capstone
p=pefile.PE(r"C:\Program Files (x86)\Steam\steamapps\common\Counter-Strike Global Offensive\game\csgo\bin\win64\client.dll")
raw=p.__data__;cs=capstone.Cs(capstone.CS_ARCH_X86,capstone.CS_MODE_64)
sec=next(s for s in p.sections if s.Name.startswith(b".text"));code=sec.get_data()
for name in sys.argv[1:]:
    off=raw.find(name.encode()+b"\0")
    if off<0:continue
    target=p.get_rva_from_offset(off);print(name,hex(target))
    for hit in re.finditer(rb"\x48\x8d\x0d",code):
        at=hit.start();rva=sec.VirtualAddress+at
        if rva+7+struct.unpack_from("<i",code,at+3)[0]!=target:continue
        print("xref",hex(rva))
        fn=next((e.struct for e in p.DIRECTORY_ENTRY_EXCEPTION if e.struct.BeginAddress<=rva<e.struct.EndAddress),None)
        if fn:
            ins=list(cs.disasm(p.get_data(fn.BeginAddress,fn.EndAddress-fn.BeginAddress),fn.BeginAddress))
            idx=next((i for i,x in enumerate(ins) if x.address==rva),0)
            for x in ins[max(0,idx-12):idx+7]:print(hex(x.address),x.mnemonic,x.op_str)
