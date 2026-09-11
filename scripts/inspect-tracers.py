import sys,pathlib,struct
sys.path.insert(0,str(pathlib.Path.home()/"AppData/Local/Vortex/tools/analysis"))
import pefile,capstone
p=pefile.PE(r"C:\Program Files (x86)\Steam\steamapps\common\Counter-Strike Global Offensive\game\csgo\bin\win64\client.dll",fast_load=True)
cs=capstone.Cs(capstone.CS_ARCH_X86,capstone.CS_MODE_64)
s=next(s for s in p.sections if s.Name.startswith(b".text"));d=s.get_data()
targets={0x1b296c4,0x1b29738,0x1b2b170,0x1b2b188,0x1ad5e90}
for i in range(len(d)-7):
    if d[i] in (0x48,0x4c) and d[i+1]==0x8d and d[i+2]&0xc7==5:
        dst=s.VirtualAddress+i+7+struct.unpack_from("<i",d,i+3)[0]
        if dst in targets:
            at=s.VirtualAddress+i
            print("\nREFERENCE",hex(dst),hex(at))
            for ins in cs.disasm(p.get_data(at,130),at):print(hex(ins.address),ins.mnemonic,ins.op_str)
