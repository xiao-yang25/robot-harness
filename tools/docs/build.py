"""Build the landing page and docs from one set of repository Markdown sources."""
from pathlib import Path
import shutil
import subprocess
import sys
import yaml

ROOT = Path(__file__).resolve().parents[2]
BUILD = ROOT / 'build-docs'


def pages(nav):
    for item in nav:
        for value in item.values():
            if isinstance(value, list):
                yield from pages(value)
            elif not value.startswith('https://'):
                yield value


def main():
    config = yaml.safe_load((ROOT / 'mkdocs.yml').read_text())
    source = BUILD / 'source'
    # Only this generated source directory is refreshed; no caller-selected deletion.
    if source.exists():
        shutil.rmtree(source)
    source.mkdir(parents=True)
    for name in pages(config['nav']):
        target = source / name
        target.parent.mkdir(parents=True, exist_ok=True)
        shutil.copy2(ROOT / name, target)
    shutil.copytree(ROOT / 'docs/assets', source / 'docs/assets', dirs_exist_ok=True)
    shutil.copy2(ROOT / 'docs/site.css', source / 'docs/site.css')
    # Refresh the complete generated artifact, including landing-page media.
    site = BUILD / 'site/robot-harness'
    if site.exists():
        shutil.rmtree(site)
    subprocess.run([sys.executable, '-m', 'mkdocs', 'build', '--strict'], cwd=ROOT, check=True)
    shutil.copy2(ROOT / 'docs/index.html', site / 'index.html')
    shutil.copytree(ROOT / 'docs/assets', site / 'assets', dirs_exist_ok=True)
    (site / '.nojekyll').touch()
    subprocess.run([sys.executable, str(ROOT / 'tools/docs/check.py'), str(site)], check=True)
    print(f'Preview: python -m http.server 8768 --bind 127.0.0.1 --directory {BUILD / "site"}')
    print('Open http://127.0.0.1:8768/robot-harness/guide/docs/index.html')


if __name__ == '__main__':
    main()
