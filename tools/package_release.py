# SPDX-License-Identifier: GPL-3.0-or-later
"""Package an already-built x64 DLL. Does not publish or upload anything."""
from pathlib import Path
import json, zipfile, hashlib
root=Path(__file__).resolve().parents[1]
meta=json.loads((root/"plugin-metadata.json").read_text())
version=meta["APP_VER"];release=root/"releases";release.mkdir(exist_ok=True)
common=["README.md","CHANGELOG.md","LICENSE","THIRD_PARTY.md","plugin-metadata.json"]
binary=release/f"KPairs-{version}-x64.zip"
admin=release/f"KPairs-{version}-plugins-admin-x64.zip"
dll=root/"work/build-x64/KPairs.dll"
data=dll.read_bytes();offset=int.from_bytes(data[60:64],"little")
assert int.from_bytes(data[offset+4:offset+6],"little")==0x8664
for archive,prefix in [(binary,"KPairs/"),(admin,"")]:
 with zipfile.ZipFile(archive,"w",zipfile.ZIP_DEFLATED,compresslevel=9) as z:
  z.write(dll,prefix+"KPairs.dll")
  for name in common:z.write(root/name,prefix+name)
  for folder in ["licenses","locale"]:
   for f in sorted((root/folder).rglob("*")):
    if f.is_file():z.write(f,prefix+f.relative_to(root).as_posix())
entry={"folder-name":"KPairs","display-name":"KPairs","version":version,"npp-compatible-versions":"[8.9.8.1,]","id":hashlib.sha256(admin.read_bytes()).hexdigest(),"repository":f"https://github.com/Krzysiu/nppKPairs/releases/download/v{version}/{admin.name}","description":meta["description"],"author":meta["author"],"homepage":meta["homepage"]}
manifest=release/"plugins-admin-x64.json";manifest.write_text(json.dumps(entry,indent=2)+"\n")
source=release/f"KPairs-{version}-src.zip"
with zipfile.ZipFile(source,"w",zipfile.ZIP_DEFLATED,compresslevel=9) as z:
 for name in common+["CMakeLists.txt",".gitignore",".clang-format"]:z.write(root/name,"KPairs/"+name)
 for folder in ["src","vendor","licenses","locale","tests","tools"]:
  for f in sorted((root/folder).rglob("*")):
   if f.is_file() and "__pycache__" not in str(f):z.write(f,"KPairs/"+f.relative_to(root).as_posix())
 z.write(manifest,"KPairs/releases/plugins-admin-x64.json")
(release/"SHA256SUMS.txt").write_text("".join(hashlib.sha256(f.read_bytes()).hexdigest()+"  "+f.name+"\n" for f in [binary,source,admin]))
with zipfile.ZipFile(binary) as z:
 assert all(n.startswith("KPairs/") for n in z.namelist())
 assert z.read("KPairs/KPairs.dll")==data
 assert "KPairs/locale/pl/LC_MESSAGES/KPairs.mo" in z.namelist()
 assert not any(n.endswith((".ps1",".bat",".exe",".cmd",".py")) for n in z.namelist())
print(f"Verified local packages: {binary.name}, {source.name}")
