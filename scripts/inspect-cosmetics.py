"""Read-only local PE evidence for the cosmetics adapter (never loads the DLL)."""
import sys, pathlib, re, struct, json
sys.path.insert(0, str(pathlib.Path.home() / 'AppData/Local/Vortex/tools/analysis'))
import pefile, capstone
path = pathlib.Path(r'C:\Program Files (x86)\Steam\steamapps\common\Counter-Strike Global Offensive\game\csgo\bin\win64\client.dll')
pe = pefile.PE(str(path)); cs = capstone.Cs(capstone.CS_ARCH_X86, capstone.CS_MODE_64)
sec = next(s for s in pe.sections if s.Name.startswith(b'.text')); raw = sec.get_data()
patterns = {
 'SetAttribute': '40 53 48 83 EC 20 48 8B D9 48 81 C1 08 02 00 00',
 'RemoveAttribute': '40 53 48 83 EC 20 48 63 81 ?? ?? ?? ?? 44 0F B7 CA',
 'InvalidateDescription': '48 89 5C 24 ?? 48 89 74 24 ?? 57 48 83 EC 20 48 8D B9 ?? ?? ?? ?? 48 8B F1',
 'SetModel': '40 53 48 83 EC ?? 48 8B D9 4C 8B C2 48 8B 0D ?? ?? ?? ?? 48 8D 54 24 40',
 'SetMask': '48 89 5C 24 ?? 48 89 74 24 ?? 57 48 83 EC ?? 48 8D 99 ?? ?? ?? ?? 48 8B 71',
 'UpdateSubclass': '4C 8B DC 53 48 81 EC ?? ?? ?? ?? 48 8B 41',
 'UpdateViewModel': '40 53 48 83 EC 20 48 8B D9 E8 ?? ?? ?? ?? 48 83 BB 88 03 00 00 00',
 'UpdateComposite': '48 89 5C 24 10 48 89 6C 24 18 48 89 74 24 20 57 41 56 41 57 48 83 EC 20 44 0F B6 F2 48 8B F9',
 'UpdateSkin': '40 55 53 41 57 48 8D AC 24 00 FE FF FF 48 81 EC 00 03 00 00 44 0F B6 FA 48 8B D9',
 'RefreshSkin': '48 89 5C 24 08 57 48 83 EC 20 8B DA 48 8B F9 E8 ?? ?? ?? ?? F6 C3 01 74 0A',
 'UpdateBody': '48 8B C4 55 48 8B EC 48 83 EC 70 48 89 58 10 48 89 70 18 48',
 'SetBody': '85 D2 0F 88 ?? ?? ?? ?? 55 56 57',
}
print('Image', hex(pe.FILE_HEADER.TimeDateStamp), hex(pe.OPTIONAL_HEADER.SizeOfImage))
result = {}
for name, pattern in patterns.items():
 regex = b''.join(b'.' if '?' in x else re.escape(bytes.fromhex(x)) for x in pattern.split())
 hits = [sec.VirtualAddress + m.start() for m in re.finditer(regex, raw, re.DOTALL)]
 print(name, [hex(a) for a in hits]); result[name] = hits
 if len(hits) == 1:
  at = hits[0]; fn = next((e.struct for e in pe.DIRECTORY_ENTRY_EXCEPTION if e.struct.BeginAddress <= at < e.struct.EndAddress), None)
  print(' bytes', pe.get_data(at,24).hex(' '), 'function', hex(fn.BeginAddress) if fn else None, hex(fn.EndAddress) if fn else None)
  if '--dis' in sys.argv:
   for i in cs.disasm(pe.get_data(at, min(300, fn.EndAddress-at) if fn else 100), at): print(f'{i.address:08x} {i.mnemonic:8} {i.op_str}')
if '--json' in sys.argv: print(json.dumps(result))
