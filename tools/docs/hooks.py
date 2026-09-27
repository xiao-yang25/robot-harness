"""Keep repository links useful in the generated, path-preserving documentation."""
import html
import os
from pathlib import Path
import posixpath
import re
import subprocess
from urllib.parse import quote, unquote, urlsplit, urlunsplit

ROOT = Path(__file__).resolve().parents[2]
REPOSITORY = 'https://github.com/xiao-yang25/robot-harness'


def on_config(config):
    revision = os.environ.get('DOCS_SOURCE_REF') or subprocess.check_output(
        ['git', 'rev-parse', 'HEAD'], cwd=ROOT, text=True).strip()
    config.extra['source_revision'] = revision
    config.copyright = (
        f'Experimental · <a href="{REPOSITORY}/tree/{quote(revision, safe="")}">'
        f'Source {html.escape(revision[:7])}</a> · Licensing pending')
    return config


def on_page_content(content, page, config, files):
    # Directory URLs are disabled, so source and output parent directories match.
    # MkDocs handles ordinary Markdown links; raw HTML and repository source links
    # need the same treatment without copying source code into the published site.
    def replace(match):
        url = urlsplit(html.unescape(match.group(3)))
        if url.scheme or url.netloc or not url.path or url.path.startswith('/'):
            return match.group(0)
        source = posixpath.normpath(posixpath.join(
            posixpath.dirname(page.file.src_uri), unquote(url.path)))
        file = files.get_file_from_path(source)
        if file is not None:
            path = posixpath.relpath(file.url, posixpath.dirname(page.url) or '.')
            target = urlunsplit(('', '', path, url.query, url.fragment))
        elif any(source == item.url for item in files.documentation_pages()):
            return match.group(0)  # Already converted by MkDocs, including README -> index.html.
        else:
            local = (ROOT / source).resolve()
            if not local.is_relative_to(ROOT) or not local.exists():
                return match.group(0)  # Final site check reports unresolved links.
            kind = 'tree' if local.is_dir() else 'blob'
            revision = quote(config.extra['source_revision'], safe='')
            target = f'{REPOSITORY}/{kind}/{revision}/{quote(source)}'
            target = urlunsplit((*urlsplit(target)[:3], url.query, url.fragment))
        return match.group(1) + match.group(2) + html.escape(target, quote=True) + match.group(2)

    return re.sub(r'(<a\b[^>]*?\bhref=)(["\'])(.*?)\2', replace, content)
