"""Start a local stdio MCP server; stdout is reserved for protocol traffic."""

import argparse
from pathlib import Path

from .config import Config
from .server import Bridge, make_server


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--config', type=Path, help='Operator JSON configuration; omitted means counter fixture')
    parser.add_argument('--output', type=Path, required=True, help='New local trace/output directory')
    parser.add_argument('--max-trials', type=int, default=3, help='Maximum distinct supported submission IDs (1–64)')
    parser.add_argument('--max-tool-calls', type=int, default=80, help='Tool-call bound; close always remains available')
    args = parser.parse_args()
    bridge = Bridge(args.output, config=Config.read(args.config),
                    max_trials=args.max_trials, max_tool_calls=args.max_tool_calls)
    make_server(bridge).run(transport='stdio')


if __name__ == '__main__':
    main()
