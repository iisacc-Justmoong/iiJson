"""Keep the JSON codec above Contracts and independent of Runtime/storage/Qt."""
from pathlib import Path
import re
import sys
root=Path(sys.argv[1])
for folder in ('src',):
    for path in (root/folder).rglob('*'):
        if path.suffix not in ('.h','.cpp'): continue
        for name in re.findall(r'#\s*include\s*[<"]([^>"]+)',path.read_text()):
            assert not name.startswith(('Qt','QAbstract','QObject','QString','QJson','iiFileProvider')), f'upper include: {path}: {name}'
            if name.startswith('iiSDKCore/'):
                assert name.startswith(('iiSDKCore/Core/',)), f'runtime include: {path}: {name}'
for path in (root/'CMakeLists.txt',root/'cmake/iiJsonConfig.cmake.in'):
    dependencies=re.findall(r'find_(?:package|dependency)\(\s*(\w+)',path.read_text())
    assert set(dependencies)<= {'iiSDKCore','Python3'}, f'upper package dependency: {path}'
print('iiJson boundary passed: standard C++ and Core Contracts only')
