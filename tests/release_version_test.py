"""Exercise release routing and validation using independent platform versions."""
import os
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
BASH = os.environ.get("TEST_BASH") or shutil.which("bash")
SCRIPT = ROOT / ".github/scripts/release-version.sh"


class ReleaseVersionTests(unittest.TestCase):
    def run_metadata(self, tag=None, platform="all", windows="1.2.3", macos="4.5.6"):
        with tempfile.TemporaryDirectory() as directory:
            work = Path(directory)
            for name, version in (("windows", windows), ("macos", macos)):
                if version is not None:
                    (work / name).mkdir()
                    (work / name / "version.h").write_text(f'#define AT_VERSION "{version}"\n')
            env = dict(os.environ, GITHUB_EVENT_NAME="push" if tag else "workflow_dispatch",
                       GITHUB_REF_TYPE="tag" if tag else "branch", GITHUB_REF_NAME=tag or "main",
                       INPUT_PLATFORM=platform, GITHUB_OUTPUT=(work / "output").as_posix())
            result = subprocess.run([BASH, SCRIPT.as_posix()], cwd=work, env=env,
                                    text=True, capture_output=True)
            output = (work / "output").read_text() if (work / "output").exists() else ""
            return result, dict(line.split("=", 1) for line in output.splitlines())

    def test_windows_tag_ignores_macos_version(self):
        result, output = self.run_metadata("windows-v1.2.3", macos=None)
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(output, {"platform": "windows", "windows-version": "1.2.3"})

    def test_macos_tag_ignores_windows_version(self):
        result, output = self.run_metadata("macos-v4.5.6", windows=None)
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(output, {"platform": "macos", "macos-version": "4.5.6"})

    def test_manual_build_keeps_both_versions(self):
        result, output = self.run_metadata()
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(output, {"platform": "all", "windows-version": "1.2.3", "macos-version": "4.5.6"})

    def test_manual_platform_selection(self):
        result, output = self.run_metadata(platform="windows", macos=None)
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(output["platform"], "windows")

    def test_mismatched_tag_is_rejected(self):
        result, _ = self.run_metadata("windows-v4.5.6")
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("windows-v1.2.3", result.stderr)

    def test_old_shared_tag_is_rejected(self):
        result, _ = self.run_metadata("v1.2.3")
        self.assertNotEqual(result.returncode, 0)

    def test_invalid_versions_are_rejected(self):
        for version in ("1.0", "01.2.3", "1.2.3-beta", "1.2.3+build", "1.2.3.4", "text", "65536.0.0"):
            with self.subTest(version=version):
                result, _ = self.run_metadata(platform="windows", windows=version)
                self.assertNotEqual(result.returncode, 0)

    def test_unknown_platform_is_rejected(self):
        result, _ = self.run_metadata(platform="linux")
        self.assertNotEqual(result.returncode, 0)


if __name__ == "__main__":
    unittest.main()
