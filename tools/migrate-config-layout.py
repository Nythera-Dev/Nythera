#!/usr/bin/env python3
"""One-shot migration of a legacy Nythera layout to the single-layer layout (issue #31, Stage 6).

Legacy:  ~/.local/state/nythera/settings.toml (the profile without flat Shell TOML or includes) and shell state
         files directly under ~/.local/state/nythera/.
New:     ~/.config/nythera/theme.toml             shared [theme]
         ~/.config/nythera/shell/config.toml      every other Shell setting
         ~/.local/state/nythera/shell/            Shell runtime state (state.toml, wallpaper paths, ...)

Subcommands
  stage   Build the new layout in a staging directory using an isolated copy of the sources. Never
          touches live directories. Verifies the staged layout against the legacy effective config.
  apply   Re-stage after both applications stop, verify, then promote the new layout.
  rollback  Move the targets from an interrupted or completed promotion into its private holding
            directories. Legacy files are never deleted.

Files configuration and state (~/.config/nythera/files, ~/.local/state/nythera/files) are never changed.
All nythera invocations run in a fully isolated environment (HOME, XDG_*, NYTHERA_*, no D-Bus).
"""

import argparse
import ctypes
import json
import os
import re
import shutil
import subprocess
import sys
import tempfile
import tomllib
from pathlib import Path

HEADER = re.compile(r"^\s*\[\[?([^\]]+?)\]\]?\s*$")
WALLPAPER_RUNTIME = {"default", "last", "monitors"}
# Shell-owned runtime state that moves to <state>/shell/ (matches ConfigService::resolveActiveStateDir users).
SHELL_STATE_ENTRIES = [
    "clipboard",
    "notification_history.enc",
    "notification_history_assets",
    "screen_time.json",
    "usage_counts.json",
    "recently_used.json",
    "wallpaper_shuffle.json",
    "instance.id",
    ".setup-complete",
    "community-templates",
    "plugin-cache",
]


def header_segments(line):
    m = HEADER.match(line)
    if not m:
        return None
    return [p.strip().strip('"') for p in re.split(r"\.(?=(?:[^\"]*\"[^\"]*\")*[^\"]*$)", m.group(1))]


def split_merged(text):
    """Split the exported merged config into (config, theme, wallpaper_runtime) text blocks."""
    config, theme, runtime = [], [], []
    current = config
    for line in text.splitlines():
        seg = header_segments(line)
        if seg is not None:
            if seg[0] == "theme":
                current = theme
            elif seg[0] == "wallpaper" and len(seg) > 1 and seg[1] in WALLPAPER_RUNTIME:
                current = runtime
            else:
                current = config
        current.append(line)
    return config, theme, runtime


def join_lines(lines):
    return "\n".join(lines).strip("\n") + "\n"


def toml_str(value):
    return json.dumps(value, ensure_ascii=False)


def toml_key(key):
    return key if re.fullmatch(r"[A-Za-z0-9_-]+", key) else toml_str(key)


def runtime_to_state(runtime_lines):
    data = tomllib.loads("\n".join(l.strip() if HEADER.match(l) else l for l in runtime_lines))
    wp = data.get("wallpaper", {})
    out = ["[wallpaper]"]
    for key in ("default", "last"):
        path = wp.get(key, {}).get("path")
        if path:
            out.append(f"{key} = {toml_str(path)}")
    monitors = {k: v["path"] for k, v in wp.get("monitors", {}).items() if v.get("path")}
    if monitors:
        out += ["", "[wallpaper.monitors]"]
        out += [f"{toml_key(k)} = {toml_str(v)}" for k, v in sorted(monitors.items())]
    return "\n".join(out) + "\n" if len(out) > 1 else ""


def strip_files_section(state_text):
    """Remove the [files] owner (migrated to ~/.local/state/nythera/files by Files itself)."""
    out, skipping = [], False
    for line in state_text.splitlines():
        seg = header_segments(line)
        if seg is not None:
            skipping = seg[0] == "files"
        if not skipping:
            out.append(line)
    return "\n".join(out).strip() + "\n"


def isolated_env(tree):
    for sub in ("config", "state", "data", "cache", "home", "run"):
        (tree / sub).mkdir(parents=True, exist_ok=True)
    (tree / "run").chmod(0o700)
    return {
        "PATH": os.environ.get("PATH", "/usr/bin"),
        "HOME": str(tree / "home"),
        "XDG_RUNTIME_DIR": str(tree / "run"),
        "NYTHERA_CONFIG_HOME": str(tree / "config"),
        "NYTHERA_STATE_HOME": str(tree / "state"),
        "NYTHERA_DATA_HOME": str(tree / "data"),
        "XDG_CONFIG_HOME": str(tree / "config"),
        "XDG_STATE_HOME": str(tree / "state"),
        "XDG_DATA_HOME": str(tree / "data"),
        "XDG_CACHE_HOME": str(tree / "cache"),
        "GIO_USE_VFS": "local",
        "DBUS_SESSION_BUS_ADDRESS": "disabled:",
        "DBUS_SYSTEM_BUS_ADDRESS": "disabled:",
        "LD_LIBRARY_PATH": os.environ.get("NYTHERA_LIB_DIR", str(Path.home() / ".local/opt/nythera-lib/lib")),
    }


def run_nythera(binary, tree, *args):
    proc = subprocess.run([binary, *args], env=isolated_env(tree), capture_output=True, text=True)
    if proc.returncode != 0:
        raise SystemExit(f"nythera {' '.join(args)} failed: {proc.stderr.strip() or proc.stdout.strip()}")
    return proc.stdout


def copy_tree_item(src, dst):
    if src.is_dir():
        shutil.copytree(src, dst, symlinks=True, dirs_exist_ok=True)
    else:
        dst.parent.mkdir(parents=True, exist_ok=True)
        shutil.copy2(src, dst)


def flatten(data, prefix=()):
    for key, value in data.items():
        if isinstance(value, dict) and value:
            yield from flatten(value, prefix + (key,))
        else:
            yield prefix + (key,), value


def build_stage(src_config, src_state, work, binary):
    """Return the staged tree path after building and verifying it."""
    if any(src_config.glob("*.toml")) or present(src_config / "shell"):
        raise SystemExit("This migration only supports the legacy profile without flat or split Shell TOML.")
    legacy, staged = work / "legacy", work / "staged"
    (legacy / "config").mkdir(parents=True)
    (legacy / "state").mkdir(parents=True)
    shutil.copytree(src_config, legacy / "config" / "nythera", symlinks=True)
    shutil.copytree(src_state, legacy / "state" / "nythera", symlinks=True)
    src_config = legacy / "config" / "nythera"
    src_state = legacy / "state" / "nythera"
    settings = src_state / "settings.toml"
    if not settings.is_file() or settings.is_symlink():
        raise SystemExit("Expected a regular legacy settings.toml in the snapshot.")
    legacy_settings = tomllib.loads(settings.read_text())
    if "include" in legacy_settings:
        raise SystemExit("This migration does not move Shell includes.")

    run_nythera(binary, legacy, "config", "validate")
    merged = run_nythera(binary, legacy, "config", "export", "merged", "--credentials", "keep")
    legacy_full = tomllib.loads(run_nythera(binary, legacy, "config", "export", "full"))
    config_lines, theme_lines, runtime_lines = split_merged(merged)

    cfg = staged / "config" / "nythera"
    st = staged / "state" / "nythera"
    (cfg / "shell").mkdir(parents=True)
    (st / "shell").mkdir(parents=True)
    for path, contents in (
        (cfg / "theme.toml", join_lines(theme_lines) if theme_lines else ""),
        (cfg / "shell" / "config.toml", join_lines(config_lines)),
    ):
        with open(path, "x", opener=private_open) as handle:
            handle.write(contents)
    # Files configuration/state and shared palettes are carried over unchanged for verification only.
    for name in ("files", "palettes"):
        if (src_config / name).exists():
            copy_tree_item(src_config / name, cfg / name)
    if (src_state / "files").exists():
        copy_tree_item(src_state / "files", st / "files")

    state_parts = []
    old_state = src_state / "state.toml"
    if old_state.is_symlink():
        raise SystemExit("Expected regular legacy state.toml in the snapshot.")
    if old_state.exists():
        kept = strip_files_section(old_state.read_text())
        if kept.strip():
            state_parts.append(kept)
    wallpaper_state = runtime_to_state(runtime_lines)
    if wallpaper_state:
        state_parts.append(wallpaper_state)
    (st / "shell" / "state.toml").write_text("\n".join(state_parts))
    (st / "shell" / "state.toml").chmod(0o600)
    for entry in SHELL_STATE_ENTRIES:
        if (src_state / entry).exists():
            copy_tree_item(src_state / entry, st / "shell" / entry)

    # --- verification -------------------------------------------------------------------------
    run_nythera(binary, staged, "config", "validate")
    new_full = tomllib.loads(run_nythera(binary, staged, "config", "export", "full"))
    a, b = dict(flatten(legacy_full)), dict(flatten(new_full))
    only_old = {k for k in a if k not in b}
    changed = {k for k in a if k in b and a[k] != b[k]}
    only_new = {k for k in b if k not in a}
    unexpected = only_old | changed | only_new
    report = [f"effective keys compared: {len(a)} legacy / {len(b)} staged"]
    if unexpected:
        report.append("UNEXPECTED DIFFERENCES:")
        report += [f"  {'.'.join(k)}" for k in sorted(unexpected)]
    new_state = tomllib.loads((st / "shell" / "state.toml").read_text())
    wp = legacy_settings.get("wallpaper", {})
    expected_wallpaper = {}
    for key in ("default", "last"):
        want = wp.get(key, {}).get("path")
        if want:
            expected_wallpaper[key] = want
    monitors = {conn: tbl["path"] for conn, tbl in wp.get("monitors", {}).items() if tbl.get("path")}
    if monitors:
        expected_wallpaper["monitors"] = monitors
    if expected_wallpaper != new_state.get("wallpaper", {}):
        unexpected.add(("state", "wallpaper"))
        report.append("STATE MISMATCH [wallpaper]")
    else:
        report.append(f"wallpaper paths verified in state: {len(expected_wallpaper) - bool(monitors) + len(monitors)}")
    old_state_data = tomllib.loads(old_state.read_text()) if old_state.exists() else {}
    for owner, tbl in old_state_data.items():
        if owner != "files" and new_state.get(owner) != tbl:
            unexpected.add(("state", owner))
            report.append(f"STATE MISMATCH [{owner}]")
    (work / "verification-report.txt").write_text("\n".join(report) + "\n")
    print("\n".join(report))
    if unexpected:
        raise SystemExit("verification failed; nothing was installed")
    return staged


def private_open(path, flags):
    return os.open(path, flags, 0o600)


def present(path):
    return path.exists() or path.is_symlink()


def resolve_home(kind, fallback):
    root = os.environ.get(f"NYTHERA_{kind}_HOME") or os.environ.get(f"XDG_{kind}_HOME")
    return (Path(root) if root else Path.home() / fallback).resolve() / "nythera"


def sync_directory(path):
    fd = os.open(path, os.O_RDONLY | os.O_DIRECTORY)
    try:
        os.fsync(fd)
    finally:
        os.close(fd)


def sync_prepared_tree(path):
    if path.is_symlink():
        return
    if path.is_dir():
        for child in path.iterdir():
            sync_prepared_tree(child)
        sync_directory(path)
    elif path.is_file():
        with path.open("rb") as handle:
            os.fsync(handle.fileno())


def rename_new(src, dst):
    # Both paths are on the same filesystem. Refuse to replace an existing name,
    # including a symlink or an empty directory created after preflight.
    renameat2 = ctypes.CDLL(None, use_errno=True).renameat2
    if renameat2(-100, os.fsencode(src), -100, os.fsencode(dst), 1) != 0:
        error = ctypes.get_errno()
        raise OSError(error, os.strerror(error), str(dst))
    sync_directory(src.parent)
    sync_directory(dst.parent)


def save_journal(path, journal):
    with tempfile.NamedTemporaryFile(mode="w", dir=path.parent, delete=False) as handle:
        json.dump(journal, handle, indent=2)
        handle.flush()
        os.fsync(handle.fileno())
    os.replace(handle.name, path)
    sync_directory(path.parent)


def live_targets(config, state):
    # Shell config goes last. Its presence switches the loader to the new layout.
    return (state / "shell", config / "theme.toml", config / "shell")


def require_empty_targets(config, state):
    for target in live_targets(config, state):
        if present(target):
            raise SystemExit(f"Refusing to replace existing {target}. Use the original transaction to roll back.")


def identity(path):
    info = path.lstat()
    return [info.st_dev, info.st_ino]


def rollback_stage(attempt, live_config, live_state):
    path = attempt / "promotion.json"
    journal = json.loads(path.read_text())
    entries = journal["entries"]
    targets = live_targets(live_config, live_state)
    if [Path(entry["target"]) for entry in entries] != list(targets):
        raise RuntimeError("The transaction does not match the active configuration and state roots.")
    if journal["rolled_back"]:
        return
    conflicts = []
    for entry, target in reversed(list(zip(entries, targets))):
        held = Path(entry["held"])
        if present(held):
            if present(target):
                conflicts.append(f"Both {target} and its holding path exist. Left both in place.")
            continue
        if not present(target):
            conflicts.append(f"Both {target} and its holding path are missing.")
            continue
        # After a completed cutover settings can atomically replace theme.toml.
        if identity(target) != entry["identity"] and not (journal["complete"] and target == live_config / "theme.toml"):
            conflicts.append(f"The identity of {target} changed. Left it in place.")
            continue
        rename_new(target, held)
        print(f"Retained {target} at {held}")
    if conflicts:
        raise RuntimeError("\n".join(conflicts))
    journal["rolled_back"] = True
    save_journal(path, journal)


def apply_stage(staged, live_config, live_state):
    require_empty_targets(live_config, live_state)
    cfg_src, st_src = staged / "config" / "nythera", staged / "state" / "nythera"
    targets = live_targets(live_config, live_state)
    sources = (st_src / "shell", cfg_src / "theme.toml", cfg_src / "shell")
    attempt = staged.parent
    journal_file = attempt / "promotion.json"
    holding = {
        root: Path(tempfile.mkdtemp(prefix=f".{attempt.name}-", dir=root)) for root in {live_config, live_state}
    }
    entries = []
    for src, target in zip(sources, targets):
        prepared = holding[target.parent] / target.name
        copy_tree_item(src, prepared)
        entries.append({"target": str(target), "held": str(prepared), "identity": identity(prepared)})
    for root, directory in holding.items():
        sync_prepared_tree(directory)
        sync_directory(root)
    journal = {"entries": entries, "complete": False, "rolled_back": False}
    save_journal(journal_file, journal)
    try:
        for entry, target in zip(entries, targets):
            rename_new(Path(entry["held"]), target)
            print(f"Installed {target}")
        journal["complete"] = True
        save_journal(journal_file, journal)
    except (OSError, KeyboardInterrupt):
        print(f"Promotion interrupted. Recovering from {journal_file}.", file=sys.stderr)
        rollback_stage(attempt, live_config, live_state)
        raise


def require_apps_stopped():
    for name in ("nythera", "nythera-file-ma"):
        check = subprocess.run(["pgrep", "-x", name], capture_output=True, text=True)
        if check.returncode != 1:
            raise SystemExit(f"Both apps must be stopped (process check for {name}: {check.stdout.strip() or check.returncode}).")


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("command", choices=["stage", "apply", "rollback"])
    ap.add_argument("--nythera", help="path to the rebuilt nythera binary (stage and apply)")
    ap.add_argument("--work", required=True, help="working directory, or the specific attempt directory for rollback")
    ap.add_argument("--source-config", type=Path, help="override the source only for an isolated stage run")
    ap.add_argument("--source-state", type=Path, help="override the source only for an isolated stage run")
    ap.add_argument("--confirm-authorised", action="store_true", help="required for apply and rollback")
    args = ap.parse_args()
    work = Path(args.work).resolve()
    live_config = resolve_home("CONFIG", ".config").resolve()
    live_state = resolve_home("STATE", ".local/state").resolve()
    if args.command != "stage":
        if not args.confirm_authorised or args.source_config or args.source_state:
            ap.error("apply and rollback require explicit authorisation and use the active profile paths")
        require_apps_stopped()
    if args.command == "rollback":
        rollback_stage(work, live_config, live_state)
        return
    if not args.nythera:
        ap.error("stage and apply need --nythera")
    source_config = (args.source_config or live_config).resolve()
    source_state = (args.source_state or live_state).resolve()
    if any(work.is_relative_to(root) for root in (live_config, live_state, source_config, source_state)):
        ap.error("--work must be outside the configuration and state directories")
    if args.command == "apply":
        require_empty_targets(live_config, live_state)
    os.umask(0o077)
    work.mkdir(parents=True, exist_ok=True)
    attempt = Path(tempfile.mkdtemp(prefix="stage6-", dir=work))
    print(f"Migration attempt: {attempt}", flush=True)
    staged = build_stage(source_config, source_state, attempt, str(Path(args.nythera).resolve()))
    if args.command == "apply":
        require_apps_stopped()
        apply_stage(staged, live_config, live_state)
    else:
        print(f"Staged layout: {staged}")


if __name__ == "__main__":
    sys.exit(main())
