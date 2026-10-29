"""Keep the JSON codec independent of other SDKs, Runtime/storage and Qt."""
from pathlib import Path
import re
import sys
root=Path(sys.argv[1])
for folder in ('src',):
    for path in (root/folder).rglob('*'):
        if path.suffix not in ('.h','.cpp'): continue
        for name in re.findall(r'#\s*include\s*[<"]([^>"]+)',path.read_text()):
            assert not name.startswith(('Qt','QAbstract','QObject','QString','QJson','iiFileProvider')), f'upper include: {path}: {name}'
            assert not name.startswith('iiSDKCore/'), f'peer SDK include: {path}: {name}'
for path in (root/'CMakeLists.txt',root/'cmake/iiJsonConfig.cmake.in'):
    dependencies=re.findall(r'find_(?:package|dependency)\(\s*(\w+)',path.read_text())
    assert set(dependencies)<= {'Python3'}, f'peer package dependency: {path}'
for name in ('Json.h','Json.cpp','Schema/Schema.h','Schema/Schema.cpp','Schema/SchemaDefinition.cpp','detail/SchemaNode.h','detail/ContractIO.h'):
    path=root/'src'/name
    includes=re.findall(r'#\s*include\s*[<"]([^>"]+)',path.read_text())
    assert not any('Ontology' in item for item in includes), f'base-to-ontology dependency: {path}'
print('iiJson boundary passed: standard C++ only; ontology depends on the codec')
