import importlib.util
import os
import tempfile
import unittest
from pathlib import Path
from unittest.mock import patch


spec = importlib.util.spec_from_file_location(
    "migrate_config_layout", Path(__file__).resolve().parents[1] / "tools/migrate-config-layout.py"
)
migration = importlib.util.module_from_spec(spec)
spec.loader.exec_module(migration)


class MigrationPromotionTest(unittest.TestCase):
    def setUp(self):
        fixture = tempfile.TemporaryDirectory(prefix="nythera-migration-test-")
        self.addCleanup(fixture.cleanup)
        self.root = Path(fixture.name)
        self.config = self.root / "live-config"
        self.state = self.root / "live-state"
        self.attempt = self.root / "attempt"
        self.staged = self.attempt / "staged"
        for root in (self.config, self.state):
            (root / "files").mkdir(parents=True)
            (root / "files/original.toml").write_text("# Files owns this\n")
        (self.state / "settings.toml").write_text("# Legacy Shell settings\n")
        for owner, name in (("config", "config.toml"), ("state", "state.toml")):
            shell = self.staged / owner / "nythera/shell"
            shell.mkdir(parents=True)
            (shell / name).write_text("# Staged Shell data\n")
            (shell / name).chmod(0o600)
        theme = self.staged / "config/nythera/theme.toml"
        theme.write_text("[theme]\nmode = 'dark'\n")
        theme.chmod(0o600)
        self.targets = migration.live_targets(self.config, self.state)

    def assert_legacy_intact(self):
        self.assertTrue(all(not migration.present(target) for target in self.targets))
        self.assertEqual((self.state / "settings.toml").read_text(), "# Legacy Shell settings\n")
        for root in (self.config, self.state):
            self.assertEqual((root / "files/original.toml").read_text(), "# Files owns this\n")
        self.assertTrue((self.staged / "config/nythera/theme.toml").is_file())

    def test_completed_cutover_and_rollback_preserve_new_edits(self):
        migration.apply_stage(self.staged, self.config, self.state)
        theme = self.config / "theme.toml"
        config = self.config / "shell/config.toml"
        self.assertEqual(theme.stat().st_mode & 0o777, 0o600)
        self.assertEqual(config.stat().st_mode & 0o777, 0o600)
        replacement = self.config / ".updated-theme"
        replacement.write_text("[theme]\nmode = 'light'\n")
        replacement.replace(theme)
        migration.rollback_stage(self.attempt, self.config, self.state)
        migration.rollback_stage(self.attempt, self.config, self.state)
        self.assert_legacy_intact()
        saved = list(self.config.glob(".attempt-*/theme.toml"))
        self.assertEqual(len(saved), 1)
        self.assertIn("light", saved[0].read_text())

    def test_copy_or_rename_failure_recovers_promoted_entries(self):
        original = migration.rename_new

        def interrupt(source, destination):
            if destination == self.config / "shell":
                raise OSError("simulated promotion failure")
            original(source, destination)

        with patch.object(migration, "rename_new", interrupt):
            with self.assertRaisesRegex(OSError, "simulated"):
                migration.apply_stage(self.staged, self.config, self.state)
        self.assert_legacy_intact()

    def test_journal_recovers_after_process_dies_mid_promotion(self):
        original = migration.rename_new

        def interrupt(source, destination):
            original(source, destination)
            if destination == self.config / "theme.toml":
                os._exit(42)

        child = os.fork()
        if child == 0:
            try:
                with patch.object(migration, "rename_new", interrupt):
                    migration.apply_stage(self.staged, self.config, self.state)
            finally:
                os._exit(1)
        _, status = os.waitpid(child, 0)
        self.assertEqual(os.waitstatus_to_exitcode(status), 42)
        self.assertTrue((self.config / "theme.toml").is_file())
        self.assertFalse((self.config / "shell").exists())
        migration.rollback_stage(self.attempt, self.config, self.state)
        self.assert_legacy_intact()

    def test_existing_dangling_symlink_is_never_replaced(self):
        theme = self.config / "theme.toml"
        theme.symlink_to("missing")
        with self.assertRaises(SystemExit):
            migration.apply_stage(self.staged, self.config, self.state)
        self.assertEqual(theme.readlink(), Path("missing"))
        self.assertFalse((self.state / "shell").exists())

    def test_resolves_nythera_then_xdg_then_home(self):
        for kind, default in (("CONFIG", ".config"), ("STATE", ".local/state")):
            with patch.dict(os.environ, {"HOME": str(self.root)}, clear=True):
                self.assertEqual(migration.resolve_home(kind, default), self.root / default / "nythera")
                os.environ[f"XDG_{kind}_HOME"] = str(self.root / "xdg")
                self.assertEqual(migration.resolve_home(kind, default), self.root / "xdg/nythera")
                os.environ[f"NYTHERA_{kind}_HOME"] = str(self.root / "nythera-home")
                self.assertEqual(migration.resolve_home(kind, default), self.root / "nythera-home/nythera")


if __name__ == "__main__":
    unittest.main()
