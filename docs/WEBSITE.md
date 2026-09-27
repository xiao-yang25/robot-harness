# Documentation development

The project homepage stays at `/robot-harness/`; generated guides live under
`/robot-harness/guide/`. Both are published together. Markdown in this repository
is the only maintained guide content. `mkdocs.yml` selects pages and groups the
navigation for users, integrators and contributors; the build copies these files
to an ignored staging directory before rendering with MkDocs Material.

## Build and preview

Requires Python 3.9+; documentation packages do not affect Core builds:

```sh
python3 -m venv build-docs-venv
build-docs-venv/bin/python -m pip install -r tools/docs/requirements.txt
build-docs-venv/bin/python -m unittest discover -s tools/docs -p 'test_*.py'
build-docs-venv/bin/python tools/docs/build.py
python3 -m http.server 8768 --bind 127.0.0.1 --directory build-docs/site
```

Open <http://127.0.0.1:8768/robot-harness/guide/docs/index.html>. The prefix matches
GitHub Pages so local navigation and media checks exercise the deployed paths.
Stop the preview server with Ctrl-C. Generated output remains in `build-docs/`;
never commit the virtual environment, staged Markdown or rendered HTML.

## Authoring and links

Edit the original Markdown, add new pages to `mkdocs.yml`, and rebuild. Keep
repository-relative Markdown links: the renderer converts them to site pages.
Links to source files and directories open the matching GitHub revision instead
of publishing a second copy of the code. The footer identifies that revision;
for local uncommitted work it identifies HEAD, so publish only a committed build.
The homepage's Docs link opens the generated reading guide. Its existing asset
and video URLs stay unchanged.

The final HTML check rejects broken local pages, media and anchors. External
links are not fetched; source availability is checked against the checkout.
Search runs in the browser with a generated index. Fonts and theme assets are
served with the site; no analytics or remote font service is configured.

## CI and publishing

The documentation workflow builds and checks pull requests without deployment.
Only a successful build from `master` can deploy, through the GitHub Pages
environment. Repository Pages must use **GitHub Actions** as its publishing
source. The workflow uses a read-only build job; Pages write and OIDC permissions
are limited to the deployment job. Failed builds cannot replace the live site.

The separate [product homepage](https://xiao-yang25.github.io/robot-harness/) remains its own HTML presentation;
MkDocs generates the guides, not a duplicate Markdown body to maintain. To change
the generator later, preserve the Markdown and public homepage/media paths.
Generated API symbol documentation, translated editions and versioned release
sites remain later work; the [API entry map](API.md) links current signatures.
