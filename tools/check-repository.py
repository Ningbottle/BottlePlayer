"""Check that maintained source and documentation form a standalone Qt tree."""
from pathlib import Path
import re
import subprocess
import sys
from native_boundary import check_native_boundary

root = Path(__file__).resolve().parents[1]
errors = []
errors.extend(check_native_boundary(root / 'native'))
for relative in ('ui', 'server', 'desktop/host', '.gitmodules', 'package.json', 'Cargo.toml'):
    if (root / relative).exists():
        errors.append(f'Inactive source entry remains: {relative}')
if not (root / 'AGENTS.md').is_file():
    errors.append('Missing shared AGENTS.md')
ignored = subprocess.run(['git', '-C', str(root), 'check-ignore', 'AGENTS.md'], capture_output=True)
if ignored.returncode != 1:
    errors.append('AGENTS.md must be trackable by Git')

skip = {'out', 'output', 'build', 'CMakeFiles', 'vcpkg_installed', '__pycache__', '.git'}
disallowed = {'.rs', '.vue', '.ts', '.tsx', '.js', '.mjs', '.cjs'}
def sources(directory):
    for path in directory.iterdir():
        if path.name in skip or path.name.startswith('build-'):
            continue
        if path.is_dir():
            yield from sources(path)
        else:
            yield path

for directory in ('desktop', 'native', 'tools', '.github'):
    for path in sources(root / directory):
        if path.suffix in disallowed:
            errors.append(f'Unexpected language source: {path.relative_to(root)}')

documents = list((root / 'docs').rglob('*.md')) + list(root.glob('*.md'))
documents += [root / 'desktop/qt/README.md', root / 'native/core/README.md']
obsolete = re.compile(r'\b(?:tauri|electron|rust|vue|pnpm|cargo)\b|ui/src|desktop/host', re.I)
for path in documents:
    text = path.read_text(encoding='utf-8')
    if obsolete.search(text):
        errors.append(f'Outdated development reference: {path.relative_to(root)}')
    for target in re.findall(r'(?<!!)\[[^\]]+\]\(([^)]+)\)', text):
        target = target.split('#')[0].strip('<>')
        if not target or re.match(r'^[a-z]+:', target, re.I):
            continue
        if not (path.parent / target).exists():
            errors.append(f'Broken link in {path.relative_to(root)}: {target}')

for filename in ('Inter-400.ttf', 'Inter-500.ttf', 'Inter-600.ttf', 'LXGWWenKaiLite-Regular.ttf', 'NotoSerifSC-400.ttf', 'EBGaramond-400.ttf', 'EBGaramond-400-italic.ttf', 'EBGaramond-500.ttf', 'LibreCaslonDisplay-400.ttf', 'MiSans-Regular.ttf', 'MiSans-Medium.ttf', 'MiSans-Demibold.ttf', 'MiSans-Bold.ttf'):
    if not (root / 'desktop/qt/assets/fonts' / filename).is_file():
        errors.append(f'Missing bundled font: {filename}')
misans_license = root / 'desktop/qt/licenses/MiSans-License-Agreement.pdf'
if not misans_license.is_file() or not misans_license.read_bytes().startswith(b'%PDF-'):
    errors.append('Missing official MiSans license PDF')
for path in (root / '.github/workflows').glob('*.yml'):
    if obsolete.search(path.read_text(encoding='utf-8')):
        errors.append(f'Outdated build workflow: {path.relative_to(root)}')

if errors:
    print('\n'.join(errors), file=sys.stderr)
    sys.exit(1)
print(f'Qt repository checks passed: {len(documents)} documents, source boundaries, workflows and bundled fonts.')
