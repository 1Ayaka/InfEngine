from __future__ import annotations

import argparse
import os
import shutil
import subprocess
import sys
import urllib.request
import zipfile

try:
    import winreg
except ImportError:
    winreg = None


_BUILDER_PACKAGES = [
    "pip",
    "setuptools",
    "wheel",
    "ordered-set",
    "nuitka",
    "Pillow",
    "imageio",
    "av",
]


def _runtime_lib_names() -> list[str]:
    version = f"{sys.version_info.major}{sys.version_info.minor}"
    return [f"python{version}.lib", "python3.lib"]


def _run(args: list[str], *, timeout: int = 20) -> subprocess.CompletedProcess:
    kwargs = {
        "stdin": subprocess.DEVNULL,
        "stdout": subprocess.PIPE,
        "stderr": subprocess.PIPE,
        "text": True,
        "encoding": "utf-8",
        "errors": "replace",
        "timeout": timeout,
    }
    if sys.platform == "win32":
        kwargs["creationflags"] = 0x08000000
    return subprocess.run(args, **kwargs)


def _runtime_installer_info_for_machine() -> tuple[str, str]:
    machine = (os.environ.get("PROCESSOR_ARCHITECTURE") or "").lower()
    if machine in {"amd64", "x86_64"}:
        return (
            "python-3.12.8-amd64.exe",
            "https://www.python.org/ftp/python/3.12.8/python-3.12.8-amd64.exe",
        )
    if machine in {"arm64", "aarch64"}:
        return (
            "python-3.12.8-arm64.exe",
            "https://www.python.org/ftp/python/3.12.8/python-3.12.8-arm64.exe",
        )
    if machine in {"x86", "i386", "i686"}:
        return (
            "python-3.12.8.exe",
            "https://www.python.org/ftp/python/3.12.8/python-3.12.8.exe",
        )
    return (
        "python-3.12.8-amd64.exe",
        "https://www.python.org/ftp/python/3.12.8/python-3.12.8-amd64.exe",
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


def _has_dev_support(root: str) -> bool:
    include_dir = os.path.join(root, "include")
    libs_dir = os.path.join(root, "libs")
    if not os.path.isfile(os.path.join(include_dir, "Python.h")):
        return False
    return any(os.path.isfile(os.path.join(libs_dir, name)) for name in _runtime_lib_names())


def _ensure_builder_packages(root: str) -> None:
    target_python = _find_python_in_root(root)
    if not target_python:
        raise SystemExit(f"No python.exe found after preparing runtime: {root}")

    completed = _run([
        target_python,
        "-m",
        "pip",
        "install",
        "--upgrade",
        *_BUILDER_PACKAGES,
    ], timeout=1800)
    if completed.returncode != 0:
        raise SystemExit(
            "Failed to prepare Python builder packages.\n"
            f"{(completed.stderr or completed.stdout or '').strip()}"
        )

    verify = _run([
        target_python,
        "-c",
        "import pip, nuitka, PIL, imageio, av; print('ok')",
    ], timeout=60)
    if verify.returncode != 0:
        raise SystemExit(
            "Python runtime was staged, but required builder packages are not importable.\n"
            f"{(verify.stderr or verify.stdout or '').strip()}"
        )


def _download_file(url: str, dest: str) -> None:
    req = urllib.request.Request(url)
    req.add_header("User-Agent", "InfEngine-Stage-Runtime/1.0")
    with urllib.request.urlopen(req, timeout=120) as resp, open(dest, "wb") as f:
        shutil.copyfileobj(resp, f)


def _runtime_bundle_path(dest_root: str) -> str:
    return os.path.join(os.path.dirname(dest_root), "runtime_bundle.zip")


def _create_runtime_bundle(dest_root: str) -> None:
    bundle_path = _runtime_bundle_path(dest_root)
    tmp_bundle = bundle_path + ".tmp"
    if os.path.isfile(tmp_bundle):
        os.remove(tmp_bundle)

    with zipfile.ZipFile(tmp_bundle, "w", compression=zipfile.ZIP_DEFLATED, compresslevel=9) as zf:
        for root, _dirs, files in os.walk(dest_root):
            rel_dir = os.path.relpath(root, os.path.dirname(dest_root))
            for filename in files:
                source_path = os.path.join(root, filename)
                archive_name = os.path.join(rel_dir, filename)
                zf.write(source_path, archive_name)

    os.replace(tmp_bundle, bundle_path)


def _installer_cache_path(dest_root: str) -> str:
    installer_name, _installer_url = _runtime_installer_info_for_machine()
    return os.path.join(os.path.dirname(dest_root), installer_name)


def _install_full_runtime(dest_root: str) -> None:
    if sys.platform != "win32":
        raise SystemExit("Bundled full Python staging is only supported on Windows.")

    parent = os.path.dirname(dest_root)
    os.makedirs(parent, exist_ok=True)
    installer_path = _installer_cache_path(dest_root)
    if not os.path.isfile(installer_path):
        _installer_name, installer_url = _runtime_installer_info_for_machine()
        print(f"Downloading official Python installer: {installer_url}")
        _download_file(installer_url, installer_path)

    shutil.rmtree(dest_root, ignore_errors=True)
    completed = _run([
        installer_path,
        "/quiet",
        "InstallAllUsers=0",
        f"TargetDir={dest_root}",
        "AssociateFiles=0",
        "PrependPath=0",
        "Shortcuts=0",
        "CompileAll=0",
        "Include_test=0",
        "Include_launcher=0",
        "InstallLauncherAllUsers=0",
        "Include_pip=1",
        "Include_dev=1",
    ], timeout=3600)
    if completed.returncode != 0:
        raise SystemExit(
            "Failed to install official Python 3.12 into the bundled runtime directory.\n"
            f"{(completed.stderr or completed.stdout or '').strip()}"
        )


def _is_usable_full_runtime(root: str) -> bool:
    python_exe = _find_python_in_root(root)
    return bool(
        python_exe
        and _is_python312(python_exe)
        and not _is_embedded_root(root)
        and _has_dev_support(root)
        and _run([python_exe, "-c", "import pip, nuitka, PIL, imageio, av; print('ok')"], timeout=60).returncode == 0
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
        if _is_usable_full_runtime(dest_root):
            print(f"Bundled full Python 3.12 already present: {existing}")
            return 0

        shutil.rmtree(dest_root, ignore_errors=True)

    parent = os.path.dirname(dest_root)
    os.makedirs(parent, exist_ok=True)

    for candidate in _candidate_python_paths():
        if not _is_python312(candidate):
            continue
        if _is_embedded_root(os.path.dirname(candidate)):
            continue
        print(f"Staging bundled Python 3.12 from: {candidate}")
        _copy_runtime(candidate, dest_root)
        _ensure_builder_packages(dest_root)
        _create_runtime_bundle(dest_root)
        staged = _find_python_in_root(dest_root)
        if staged and _is_usable_full_runtime(dest_root):
            print(f"Bundled Python 3.12 staged to: {dest_root}")
            return 0

    print("Installing bundled full Python 3.12 from official installer...")
    _install_full_runtime(dest_root)
    _ensure_builder_packages(dest_root)
    _create_runtime_bundle(dest_root)
    staged = _find_python_in_root(dest_root)
    if staged and _is_usable_full_runtime(dest_root):
        print(f"Bundled full Python 3.12 staged from official installer: {dest_root}")
        return 0

    raise SystemExit(
        "Unable to find a usable Python 3.12 installation to stage into packaging/runtime/python312. "
        "Set INFENGINE_BUNDLED_PYTHON_ROOT or INFENGINE_BUNDLED_PYTHON_EXE if you want to override auto-detection."
    )


if __name__ == "__main__":
    raise SystemExit(main())