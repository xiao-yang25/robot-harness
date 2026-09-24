"""Validate an optional Ubuntu archive URL, also used by the host CLI."""
import os
from pathlib import Path
import unicodedata
from urllib.parse import urlsplit, urlunsplit


def validate_mirror_url(value):
    message = 'Ubuntu mirror must be an HTTPS archive URL without credentials, whitespace or controls'
    if any(character.isspace() or unicodedata.category(character) == 'Cc' for character in value):
        raise ValueError(message)
    try:
        parsed = urlsplit(value)
        if (parsed.scheme != 'https' or not parsed.hostname or parsed.username is not None
                or parsed.password is not None or parsed.query or parsed.fragment):
            raise ValueError(message)
        parsed.port  # Reject malformed ports before passing anything to Docker.
    except ValueError:
        raise ValueError(message) from None
    return urlunsplit(('https', parsed.netloc, parsed.path.rstrip('/'), '', ''))


if __name__ == '__main__':
    value = os.environ.get('UBUNTU_MIRROR', '')
    if value:
        mirror = validate_mirror_url(value)
        sources = Path('/etc/apt/sources.list')
        text = sources.read_text()
        for origin in ('http://archive.ubuntu.com/ubuntu', 'http://security.ubuntu.com/ubuntu'):
            text = text.replace(origin, mirror)
        sources.write_text(text)
