"""Read-only ABI evidence for the locally installed client.dll."""
import sys, pathlib, struct
sys.path.insert(0, str(pathlib.Path.home() / "AppData/Local/Vortex/tools/analysis"))
import pefile, capstone
path = pathlib.Path(r"C:\Program Files (x86)\Steam\steamapps\common\Counter-Strike Global Offensive\game\csgo\bin\win64\client.dll")
pe = pefile.PE(str(path), fast_load=True)
base = pe.OPTIONAL_HEADER.ImageBase
cs = capstone.Cs(capstone.CS_ARCH_X86, capstone.CS_MODE_64)
def dis(at, size=120):
    print("\nRVA", hex(at))
    for i in cs.disasm(pe.get_data(at, size), at):
        print(f"{i.address:08x} {i.mnemonic:8} {i.op_str}")
print("Timestamp", pe.FILE_HEADER.TimeDateStamp, "ImageSize", pe.OPTIONAL_HEADER.SizeOfImage)
if len(sys.argv) > 1:
    for arg in sys.argv[1:]:
        at, *count = arg.split(":")
        dis(int(at,16), int(count[0]) if count else 160)
else:
    data = pe.get_data(28253024, 24*8)
    print("Event vtable RVAs", [hex(v-base) for v in struct.unpack("<24Q", data)])
    dis(0x8a43f0)
    dis(0x82b5d0, 280)
    dis(0xb6a3a0, 320)
    dis(0xb636c0, 200)
    text = next(s for s in pe.sections if s.Name.startswith(b".text"))
    raw = text.get_data()
    for field in (0x13b0, 0x13b4):
        needle = struct.pack("<I",field)
        hits=[]; at=0
        while len(hits)<12:
            at=raw.find(needle,at)
            if at<0: break
            hits.append(hex(text.VirtualAddress+at));at+=4
        print("Field",hex(field),"references",hits)
