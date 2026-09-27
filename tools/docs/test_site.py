from pathlib import Path
import tempfile
import unittest
from types import SimpleNamespace

from hooks import on_page_content

from check import check


class SiteLinksTest(unittest.TestCase):
    def test_paths_anchors_and_media_under_project_prefix(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            (root / 'guide').mkdir()
            (root / 'index.html').write_text('<a href="guide/page.html#entry">Guide</a>')
            (root / 'guide/page.html').write_text(
                '<h1 id="entry">Start</h1><a href="/robot-harness/">Home</a>'
                '<img src="../image.svg"><a href="https://example.com">External</a>')
            (root / 'image.svg').write_text('<svg/>')
            self.assertEqual(check(root), (2, []))

    def test_missing_anchor_file_and_wrong_base_path_fail(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            (root / 'index.html').write_text(
                '<a href="#absent">Anchor</a><video poster="missing.png"></video>'
                '<a href="/guide/">Wrong prefix</a><img src="../outside.png">')
            count, errors = check(root)
            self.assertEqual(count, 1)
            self.assertEqual(len(errors), 4)
            self.assertTrue(any('missing anchor' in error for error in errors))
            self.assertTrue(any('missing missing.png' in error for error in errors))
            self.assertTrue(any('outside project URL' in error for error in errors))
            self.assertTrue(any('outside site' in error for error in errors))

    def test_source_set_and_encoded_anchor(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            (root / 'index.html').write_text(
                '<a id="hello-world"></a><a href="#hello%2Dworld">Anchor</a>'
                '<source srcset="small.svg 1x, large.svg 2x">')
            (root / 'small.svg').write_text('<svg/>')
            self.assertEqual(len(check(root)[1]), 1)
            (root / 'large.svg').write_text('<svg/>')
            self.assertEqual(check(root)[1], [])


class LinkRenderingTest(unittest.TestCase):
    def setUp(self):
        self.page = SimpleNamespace(file=SimpleNamespace(src_uri='examples/installed_core/README.md'),
                                    url='examples/installed_core/index.html')
        self.config = SimpleNamespace(extra={'source_revision': 'test-revision'})
        self.files = SimpleNamespace(
            get_file_from_path=lambda path: SimpleNamespace(url='docs/index.html')
            if path == 'docs/README.md' else None,
            documentation_pages=lambda: [SimpleNamespace(url='docs/index.html')])

    def test_rendered_readme_link_is_not_reinterpreted_as_landing_source(self):
        text = '<a href="../../docs/index.html#connect-a-backend">Backend</a>'
        self.assertEqual(on_page_content(text, self.page, self.config, self.files), text)

    def test_raw_markdown_and_source_links_have_distinct_destinations(self):
        text = '<a href="../../docs/README.md#connect-a-backend">Backend</a>'
        result = on_page_content(text, self.page, self.config, self.files)
        self.assertIn('../../docs/index.html#connect-a-backend', result)
        text = '<a href="../../include/robot_harness/authority_gate.hpp">Header</a>'
        result = on_page_content(text, self.page, self.config, self.files)
        self.assertIn('/blob/test-revision/include/robot_harness/authority_gate.hpp', result)

    def test_unknown_source_remains_for_final_link_checker_to_reject(self):
        text = '<a href="../../missing-source.hpp">Missing</a>'
        self.assertEqual(on_page_content(text, self.page, self.config, self.files), text)


if __name__ == '__main__':
    unittest.main()
