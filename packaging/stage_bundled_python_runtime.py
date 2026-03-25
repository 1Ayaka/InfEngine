from __future__ import annotations

import argparse
import os
import shutil
import subprocess
import sys
import tempfile
import urllib.request
import zipfile
from pathlib import Path

try:
    import winreg
except ImportError:
    winreg = None


_BUILDER_PACKAGES = [
    "pip",
    "setuptools",
    "wheel",
    "virtualenv",
    "ordered-set",
    "nuitka",
    "Pillow",
    "imageio",
    "av",
]


def _run(args: list[str], *, timeout: int = 20) -> subprocess.CompletedProcess:
    kwargs = {
        "stdin": subprocess.DEVNULL,
        "stdout": subprocess.PIPE,
        "stderr": subprocess.PIPE,
        "text": True,
        "timeout": timeout,
    }
    if sys.platform == "win32":
        kwargs["creationflags"] = 0x08000000
    return subprocess.run(args, **kwargs)


def _runtime_embed_info_for_machine() -> tuple[str, str]:
    machine = (os.environ.get("PROCESSOR_ARCHITECTURE") or "").lower()
    if machine in {"amd64", "x86_64"}:
        return (
            "python-3.12.8-embed-amd64.zip",
            "https://www.python.org/ftp/python/3.12.8/python-3.12.8-embed-amd64.zip",
        )
    if machine in {"arm64", "aarch64"}:
        return (
            "python-3.12.8-embed-arm64.zip",
            "https://www.python.org/ftp/python/3.12.8/python-3.12.8-embed-arm64.zip",
        )
    if machine in {"x86", "i386", "i686"}:
        return (
            "python-3.12.8-embed-win32.zip",
            "https://www.python.org/ftp/python/3.12.8/python-3.12.8-embed-win32.zip",
        )
    return (
        "python-3.12.8-embed-amd64.zip",
        "https://www.python.org/ftp/python/3.12.8/python-3.12.8-embed-amd64.zip",
    )


def _is_python312(python_exe: str) -> bool:
    if not python_exe or not os.path.isfile(python_exe):
        return False

    completed = _run([
        python_exe,
        "-c",
        "import sys; print(f'{sys.version_info.major}.{sys.version_info.minor}')",
    ])
    return completed.returncode == 0 and (completed.stdout or "").strip() == "3.12"


def _find_python_in_root(root: str) -> str | None:
    if not root or not os.path.isdir(root):
        return None

    direct_candidates = [
        os.path.join(root, "python.exe"),
        os.path.join(root, "Python.exe"),
        os.path.join(root, "Python312", "python.exe"),
    ]
    for candidate in direct_candidates:
        if _is_python312(candidate):
            return candidate

    for current_root, _dirs, files in os.walk(root):
        for filename in files:
            if filename.lower() != "python.exe":
                continue
            candidate = os.path.join(current_root, filename)
            if _is_python312(candidate):
                return candidate
    return None


def _pth_files(root: str) -> list[str]:
    if not root or not os.path.isdir(root):
        return []
    return [
        os.path.join(root, name)
        for name in os.listdir(root)
        if name.lower().endswith("._pth") and os.path.isfile(os.path.join(root, name))
    ]


def _is_embedded_root(root: str) -> bool:
    return bool(_pth_files(root))


def _enable_site_for_embedded_runtime(root: str) -> None:
    required_lines = ["python312.zip", ".", "Lib", "Lib/site-packages"]
    for pth_path in _pth_files(root):
        try:
            with open(pth_path, "r", encoding="utf-8") as f:
                raw_lines = [line.rstrip("\r\n") for line in f]
        except OSError:
            continue

        preserved: list[str] = []
        seen: set[str] = set()
        for line in raw_lines:
            stripped = line.strip()
            if not stripped or stripped.startswith("#"):
                preserved.append(line)
                continue
            if stripped == "import site":
                continue
            if stripped not in seen:
                preserved.append(stripped)
                seen.add(stripped)

        for item in required_lines:
            if item not in seen:
                preserved.append(item)
                seen.add(item)
        preserved.append("import site")

        with open(pth_path, "w", encoding="utf-8", newline="\n") as f:
            f.write("\n".join(preserved).rstrip() + "\n")


def _ensure_builder_packages(root: str) -> None:
    if not _is_embedded_root(root):
        return

    _enable_site_for_embedded_runtime(root)
    site_packages = os.path.join(root, "Lib", "site-packages")
    shutil.rmtree(site_packages, ignore_errors=True)
    os.makedirs(site_packages, exist_ok=True)

    completed = _run([
        sys.executable,
        "-m",
        "pip",
        "install",
        "--upgrade",
        "--target",
        site_packages,
        *_BUILDER_PACKAGES,
    ], timeout=1800)
    if completed.returncode != 0:
        raise SystemExit(
            "Failed to prepare embeddable Python builder packages.\n"
            f"{(completed.stderr or completed.stdout or '').strip()}"
        )

    target_python = _find_python_in_root(root)
    if not target_python:
        raise SystemExit(f"No python.exe found after preparing embeddable runtime: {root}")

    verify = _run([
        target_python,
        "-c",
        "import pip, virtualenv, nuitka, PIL, imageio, av; print('ok')",
    ], timeout=60)
    if verify.returncode != 0:
        raise SystemExit(
            "Embeddable Python runtime was staged, but required builder packages are not importable.\n"
            f"{(verify.stderr or verify.stdout or '').strip()}"
        )


def _download_file(url: str, dest: str) -> None:
    req = urllib.request.Request(url)
    req.add_header("User-Agent", "InfEngine-Stage-Runtime/1.0")
    with urllib.request.urlopen(req, timeout=120) as resp, open(dest, "wb") as f:
        shutil.copyfileobj(resp, f)


def _wheelhouse_root(dest_root: str) -> str:
    return os.path.join(os.path.dirname(dest_root), "wheels")


def _ensure_builder_wheelhouse(dest_root: str) -> None:
    wheelhouse = _wheelhouse_root(dest_root)
    shutil.rmtree(wheelhouse, ignore_errors=True)
    os.makedirs(wheelhouse, exist_ok=True)
    completed = _run([
        sys.executable,
        "-m",
        "pip",
        "download",
        "--dest",
        wheelhouse,
        *_BUILDER_PACKAGES,
    ], timeout=1800)
    if completed.returncode != 0:
        raise SystemExit(
            "Failed to prepare offline wheelhouse for bundled Python builder packages.\n"
            f"{(completed.stderr or completed.stdout or '').strip()}"
        )


def _extract_embeddable_runtime(dest_root: str) -> None:
    archive_name, archive_url = _runtime_embed_info_for_machine()
    parent = os.path.dirname(dest_root)
    os.makedirs(parent, exist_ok=True)

    with tempfile.TemporaryDirectory(prefix="infengine-embed-") as tmp_dir:
        archive_path = os.path.join(tmp_dir, archive_name)
        _download_file(archive_url, archive_path)

        extract_root = os.path.join(tmp_dir, "python312")
        os.makedirs(extract_root, exist_ok=True)
        with zipfile.ZipFile(archive_path, "r") as zf:
            zf.extractall(extract_root)

        shutil.rmtree(dest_root, ignore_errors=True)
        shutil.copytree(extract_root, dest_root)


def _stage_embeddable_runtime(dest_root: str) -> None:
    print("Migrating bundled runtime to official embeddable Python 3.12...")
    _extract_embeddable_runtime(dest_root)
    _ensure_builder_packages(dest_root)
    _ensure_builder_wheelhouse(dest_root)


def _is_usable_embeddable_runtime(root: str) -> bool:
    python_exe = _find_python_in_root(root)
    return bool(
        python_exe
        and _is_python312(python_exe)
        and _is_embedded_root(root)
        and _run([python_exe, "-c", "import pip, virtualenv, nuitka, PIL, imageio, av; print('ok')"], timeout=60).returncode == 0
        and os.path.isdir(_wheelhouse_root(root))
    )


def _registry_candidates() -> list[str]:
    if winreg is None:
        return []

    candidates: list[str] = []
    keys = [
        (winreg.HKEY_CURRENT_USER, r"SOFTWARE\Python\PythonCore\3.12\InstallPath"),
        (winreg.HKEY_LOCAL_MACHINE, r"SOFTWARE\Python\PythonCore\3.12\InstallPath"),
        (winreg.HKEY_LOCAL_MACHINE, r"SOFTWARE\WOW6432Node\Python\PythonCore\3.12\InstallPath"),
    ]

    for hive, subkey in keys:
        try:
            with winreg.OpenKey(hive, subkey) as key:
                install_path, _ = winreg.QueryValueEx(key, None)
        except OSError:
            continue

        if install_path:
            candidates.append(os.path.join(install_path, "python.exe"))
    return candidates


def _candidate_python_paths() -> list[str]:
    candidates: list[str] = []

    explicit_root = os.environ.get("INFENGINE_BUNDLED_PYTHON_ROOT")
    explicit_exe = os.environ.get("INFENGINE_BUNDLED_PYTHON_EXE")
    if explicit_exe:
        candidates.append(explicit_exe)
    if explicit_root:
        found = _find_python_in_root(explicit_root)
        if found:
            candidates.append(found)

    local_app_data = os.environ.get("LOCALAPPDATA")
    program_files = os.environ.get("ProgramFiles")

    if local_app_data:
        candidates.append(os.path.join(local_app_data, "InfEngineHub", "runtime", "python312", "python.exe"))
        candidates.append(os.path.join(local_app_data, "Programs", "Python", "Python312", "python.exe"))
    if program_files:
        candidates.append(os.path.join(program_files, "Python312", "python.exe"))

    py_launcher = _run(["py", "-3.12", "-c", "import sys; print(sys.executable)"])
    if py_launcher.returncode == 0:
        value = (py_launcher.stdout or "").strip().splitlines()
        if value:
            candidates.append(value[-1].strip())

    candidates.extend(_registry_candidates())

    current_python = sys.executable
    if current_python:
        candidates.append(current_python)

    deduped: list[str] = []
    seen: set[str] = set()
    for candidate in candidates:
        normalized = os.path.normcase(os.path.abspath(candidate))
        if normalized in seen:
            continue
        seen.add(normalized)
        deduped.append(candidate)
    return deduped


def _copy_runtime(source_python: str, dest_root: str) -> None:
    source_root = os.path.dirname(source_python)
    if os.path.normcase(os.path.abspath(source_root)) == os.path.normcase(os.path.abspath(dest_root)):
        return

    shutil.rmtree(dest_root, ignore_errors=True)
    shutil.copytree(
        source_root,
        dest_root,
        ignore=shutil.ignore_patterns("__pycache__", "*.pyc", "*.pyo"),
    )


def main() -> int:
    if sys.version_info[:2] != (3, 12):
        current = os.path.normcase(os.path.abspath(sys.executable))
        for candidate in _candidate_python_paths():
            if not _is_python312(candidate):
                continue
            if os.path.normcase(os.path.abspath(candidate)) == current:
                continue
            completed = subprocess.run([candidate, __file__, *sys.argv[1:]])
            return completed.returncode
        raise SystemExit(
            f"This staging script must run under Python 3.12, but got {sys.version.split()[0]} from {sys.executable}."
        )

    parser = argparse.ArgumentParser()
    parser.add_argument("--dest-root", required=True)
    args = parser.parse_args()

    dest_root = os.path.abspath(args.dest_root)
    existing = _find_python_in_root(dest_root)
    if existing and _is_python312(existing):
        if _is_usable_embeddable_runtime(dest_root):
            print(f"Bundled embeddable Python 3.12 already present: {existing}")
            return 0

        if _is_embedded_root(dest_root):
            _ensure_builder_packages(dest_root)
            _ensure_builder_wheelhouse(dest_root)
            print(f"Bundled embeddable Python 3.12 prepared: {existing}")
            return 0

        _stage_embeddable_runtime(dest_root)
        staged = _find_python_in_root(dest_root)
        if staged and _is_usable_embeddable_runtime(dest_root):
            print(f"Bundled Python 3.12 migrated to embeddable runtime: {dest_root}")
            return 0
        raise SystemExit(f"Failed to migrate bundled runtime at {dest_root}")

    parent = os.path.dirname(dest_root)
    os.makedirs(parent, exist_ok=True)

    for candidate in _candidate_python_paths():
        if not _is_python312(candidate):
            continue
        print(f"Staging bundled Python 3.12 from: {candidate}")
        _copy_runtime(candidate, dest_root)
        _ensure_builder_packages(dest_root)
        _ensure_builder_wheelhouse(dest_root)
        staged = _find_python_in_root(dest_root)
        if staged and _is_python312(staged):
            print(f"Bundled Python 3.12 staged to: {dest_root}")
            return 0

    _stage_embeddable_runtime(dest_root)
    staged = _find_python_in_root(dest_root)
    if staged and _is_usable_embeddable_runtime(dest_root):
        print(f"Bundled Python 3.12 staged from official embeddable package: {dest_root}")
        return 0

    raise SystemExit(
        "Unable to find a usable Python 3.12 installation to stage into packaging/runtime/python312. "
        "Set INFENGINE_BUNDLED_PYTHON_ROOT or INFENGINE_BUNDLED_PYTHON_EXE if you want to override auto-detection."
    )


if __name__ == "__main__":
    raise SystemExit(main())