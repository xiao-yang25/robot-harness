"""Check generated local page, anchor and media links under the project URL prefix."""
from html.parser import HTMLParser
from pathlib import Path
import sys
from urllib.parse import unquote, urlsplit


class Page(HTMLParser):
    def __init__(self, text):
        super().__init__()
        self.ids = set()
        self.links = []
        self.feed(text)

    def handle_starttag(self, tag, attrs):
        attrs = dict(attrs)
        if 'id' in attrs:
            self.ids.add(attrs['id'])
        if tag == 'a' and 'name' in attrs:
            self.ids.add(attrs['name'])
        for key in ('href', 'src', 'poster'):
            if key in attrs:
                self.links.append(attrs[key])
        for candidate in attrs.get('srcset', '').split(','):
            if candidate.strip():
                self.links.append(candidate.strip().split()[0])


def check(root):
    root = root.resolve()
    pages = {p: Page(p.read_text()) for p in root.rglob('*.html')}
    errors = []
    for path, page in pages.items():
        for link in page.links:
            url = urlsplit(link)
            if url.scheme or url.netloc:
                continue
            part = unquote(url.path)
            if part.startswith('/'):
                if not part.startswith('/robot-harness/'):
                    errors.append(f'{path.relative_to(root)}: outside project URL: {link}')
                    continue
                target = root / part.removeprefix('/robot-harness/')
            else:
                target = path.parent / part if part else path
            target = target.resolve()
            if not target.is_relative_to(root):
                errors.append(f'{path.relative_to(root)}: outside site: {link}')
                continue
            if target.is_dir():
                target /= 'index.html'
            if not target.is_file():
                errors.append(f'{path.relative_to(root)}: missing {link}')
            elif url.fragment and target in pages and unquote(url.fragment) not in pages[target].ids:
                errors.append(f'{path.relative_to(root)}: missing anchor {link}')
    return len(pages), errors


if __name__ == '__main__':
    count, errors = check(Path(sys.argv[1]))
    print(f'{count} generated HTML pages checked; {len(errors)} broken local links')
    for error in errors:
        print(error)
    sys.exit(bool(errors))
