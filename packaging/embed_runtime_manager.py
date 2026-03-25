from __future__ import annotations

import os
import platform
import shutil
import subprocess
import sys
import tempfile
import urllib.error
import urllib.request
import zipfile
from pathlib import Path
from typing import Callable, Optional

from hub_utils import get_bundle_dir, get_hub_data_dir, is_frozen


_NO_WINDOW = 0x08000000 if sys.platform == "win32" else 0
_RUNTIME_ROOT = Path.home() / ".infengine" / "runtime"
_TEMPLATE_BUILDER_PACKAGES = [
    "nuitka",
    "ordered-set",
    "Pillow",
    "imageio",
    "av",
]
_RUNTIME_PACKAGES = [
    "pip",
    "setuptools",
    "wheel",
    "virtualenv",
    *_TEMPLATE_BUILDER_PACKAGES,
]


class PythonRuntimeError(RuntimeError):
    pass


def _default_runtime_dir() -> str:
    if is_frozen():
        return os.path.join(get_hub_data_dir(), "runtime")

    local_app_data = os.environ.get("LOCALAPPDATA")
    if local_app_data:
        return os.path.join(local_app_data, "InfEngineHub", "runtime")
    return str(_RUNTIME_ROOT)


def _legacy_runtime_dir() -> str:
    local_app_data = os.environ.get("LOCALAPPDATA")
    if local_app_data:
        return os.path.join(local_app_data, "InfEngineHub", "runtime")
    return str(_RUNTIME_ROOT)


def _emit_status(callback: Optional[Callable[[str], None]], message: str) -> None:
    if callback is not None:
        callback(message)


def _runtime_embed_info_for_machine() -> tuple[str, str]:
    machine = (platform.machine() or os.environ.get("PROCESSOR_ARCHITECTURE") or "").lower()
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


def _run_command(args: list[str], *, timeout: int, raise_on_error: bool = False) -> subprocess.CompletedProcess:
    kwargs = {
        "stdin": subprocess.DEVNULL,
        "stdout": subprocess.PIPE,
        "stderr": subprocess.PIPE,
        "text": True,
    }
    if sys.platform == "win32":
        kwargs["creationflags"] = _NO_WINDOW

    try:
        return subprocess.run(args, timeout=timeout, check=raise_on_error, **kwargs)
    except FileNotFoundError as exc:
        if not raise_on_error:
            return subprocess.CompletedProcess(args=args, returncode=1, stdout="", stderr=str(exc))
        raise PythonRuntimeError(str(exc)) from exc
    except OSError as exc:
        if not raise_on_error:
            return subprocess.CompletedProcess(args=args, returncode=1, stdout="", stderr=str(exc))
        raise PythonRuntimeError(str(exc)) from exc
    except subprocess.TimeoutExpired as exc:
        raise PythonRuntimeError(f"Command timed out after {timeout} seconds.\n{subprocess.list2cmdline(args)}") from exc
    except subprocess.CalledProcessError as exc:
        details = (exc.stderr or exc.stdout or "").strip()
        raise PythonRuntimeError(
            f"Command failed with exit code {exc.returncode}.\n{subprocess.list2cmdline(args)}\n{details}"
        ) from exc


def _find_python_in_root(root: str) -> Optional[str]:
    if not root or not os.path.isdir(root):
        return None

    direct_candidates = [
        os.path.join(root, "python.exe"),
        os.path.join(root, "Python.exe"),
        os.path.join(root, "Python312", "python.exe"),
        os.path.join(root, "bin", "python"),
    ]
    for candidate in direct_candidates:
        if os.path.isfile(candidate):
            return candidate

    for current_root, _dirs, files in os.walk(root):
        for filename in files:
            if sys.platform == "win32":
                if filename.lower() != "python.exe":
                    continue
            elif filename != "python":
                continue
            return os.path.join(current_root, filename)
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
        with open(pth_path, "r", encoding="utf-8") as f:
            raw_lines = [line.rstrip("\r\n") for line in f]

        output: list[str] = []
        seen: set[str] = set()
        for line in raw_lines:
            stripped = line.strip()
            if not stripped or stripped.startswith("#"):
                output.append(line)
                continue
            if stripped == "import site":
                continue
            if stripped not in seen:
                output.append(stripped)
                seen.add(stripped)

        for item in required_lines:
            if item not in seen:
                output.append(item)
                seen.add(item)
        output.append("import site")

        normalized_raw = [line.rstrip("\r\n") for line in raw_lines]
        if output == normalized_raw:
            continue

        with open(pth_path, "w", encoding="utf-8", newline="\n") as f:
            f.write("\n".join(output).rstrip() + "\n")


def _embedded_runtime_has_site_enabled(root: str) -> bool:
    required_lines = {"python312.zip", ".", "Lib", "Lib/site-packages", "import site"}
    pth_paths = _pth_files(root)
    if not pth_paths:
        return True

    for pth_path in pth_paths:
        try:
            with open(pth_path, "r", encoding="utf-8") as f:
                lines = {line.strip() for line in f if line.strip() and not line.strip().startswith("#")}
        except OSError:
            return False
        if not required_lines.issubset(lines):
            return False
    return True


def _is_python312(python_exe: str) -> bool:
    if not python_exe or not os.path.isfile(python_exe):
        return False

    completed = _run_command(
        [python_exe, "-c", "import sys; print(f'{sys.version_info.major}.{sys.version_info.minor}')"],
        timeout=20,
        raise_on_error=False,
    )
    return completed.returncode == 0 and (completed.stdout or "").strip() == "3.12"


def _site_packages_root(runtime_root: str) -> str:
    path = os.path.join(runtime_root, "Lib", "site-packages")
    os.makedirs(path, exist_ok=True)
    return path


def _download_file(url: str, dest: str, *, user_agent: str, timeout: int = 120) -> None:
    req = urllib.request.Request(url)
    req.add_header("User-Agent", user_agent)
    with urllib.request.urlopen(req, timeout=timeout) as resp, open(dest, "wb") as f:
        shutil.copyfileobj(resp, f)


class PythonRuntimeManager:
    def __init__(self, runtime_dir: Optional[str] = None) -> None:
        _RUNTIME_ROOT.mkdir(parents=True, exist_ok=True)
        self._runtime_dir = os.path.abspath(runtime_dir) if runtime_dir else _default_runtime_dir()

    def installed_runtime_dir(self) -> str:
        return self._runtime_dir

    def bundled_runtime_dirs(self) -> list[str]:
        dirs = [
            os.path.join(get_bundle_dir(), "InfEngineHubData", "runtime"),
            os.path.join(get_bundle_dir(), "runtime"),
        ]
        result: list[str] = []
        seen: set[str] = set()
        for path in dirs:
            norm = os.path.normcase(os.path.abspath(path))
            if norm in seen:
                continue
            seen.add(norm)
            result.append(path)
        return result

    def private_runtime_root(self) -> str:
        return os.path.join(self.installed_runtime_dir(), "python312")

    def private_runtime_python(self) -> str:
        if sys.platform == "win32":
            return os.path.join(self.private_runtime_root(), "python.exe")
        return os.path.join(self.private_runtime_root(), "bin", "python")

    def runtime_archive_path(self) -> str:
        archive_name, _archive_url = _runtime_embed_info_for_machine()
        return os.path.join(self.installed_runtime_dir(), archive_name)

    def bundled_archive_paths(self) -> list[str]:
        archive_name, _archive_url = _runtime_embed_info_for_machine()
        return [os.path.join(path, archive_name) for path in self.bundled_runtime_dirs()]

    def venv_template_root(self) -> str:
        return os.path.join(self.installed_runtime_dir(), "venv_template")

    def venv_template_python(self) -> str:
        if sys.platform == "win32":
            return os.path.join(self.venv_template_root(), "Scripts", "python.exe")
        return os.path.join(self.venv_template_root(), "bin", "python")

    def has_runtime(self) -> bool:
        return bool(self.get_runtime_path())

    def has_venv_template(self) -> bool:
        python_exe = self.venv_template_python()
        return self._is_valid_venv(self.venv_template_root()) and self._has_modules(
            python_exe,
            "nuitka",
            "PIL",
            "imageio",
            "av",
        )

    def get_runtime_path(self) -> Optional[str]:
        roots = [self.private_runtime_root()]
        if not is_frozen():
            roots.append(os.path.join(_legacy_runtime_dir(), "python312"))
        for root in roots:
            candidate = _find_python_in_root(root)
            if candidate and _is_python312(candidate):
                return candidate
        return None

    def ensure_runtime(self, *, on_status: Optional[Callable[[str], None]] = None) -> str:
        python_exe = self.get_runtime_path()
        if not python_exe:
            python_exe = self._provision_embedded_runtime(on_status=on_status)
        else:
            runtime_root = os.path.dirname(python_exe)
            if is_frozen():
                if _is_embedded_root(runtime_root) and not _embedded_runtime_has_site_enabled(runtime_root):
                    raise PythonRuntimeError(
                        "The installed embedded Python runtime is incomplete.\n"
                        "Please reinstall InfEngine Hub so the runtime can be prepared during installation."
                    )
                if not self._has_modules(python_exe, "pip", "virtualenv", "nuitka", "PIL", "imageio", "av"):
                    raise PythonRuntimeError(
                        "The installed embedded Python runtime is missing required support packages.\n"
                        "Please reinstall InfEngine Hub so the runtime can be prepared during installation."
                    )
                if not self.has_venv_template():
                    raise PythonRuntimeError(
                        "The installed embedded Python runtime is missing the reusable venv template.\n"
                        "Please reinstall InfEngine Hub so the runtime can be prepared during installation."
                    )
                return python_exe

            self._prepare_embedded_runtime(python_exe, on_status=on_status)

        self._ensure_venv_template(python_exe, on_status=on_status)
        return python_exe

    def create_venv(self, venv_path: str) -> str:
        python_exe = self.ensure_runtime()
        if not self.has_venv_template():
            self._ensure_venv_template(python_exe)

        try:
            shutil.copytree(self.venv_template_root(), venv_path)
        except OSError as exc:
            raise PythonRuntimeError(
                f"Failed to copy the prepared virtual environment template to {venv_path}.\n{exc}"
            ) from exc

        self._rewrite_pyvenv_cfg(venv_path, python_exe)

        if sys.platform == "win32":
            venv_python = os.path.join(venv_path, "Scripts", "python.exe")
        else:
            venv_python = os.path.join(venv_path, "bin", "python")

        if not os.path.isfile(venv_python):
            raise PythonRuntimeError(
                f"Virtual environment creation finished, but python.exe was not found at {venv_python}."
            )
        return venv_python

    def _provision_embedded_runtime(self, *, on_status: Optional[Callable[[str], None]] = None) -> str:
        runtime_root = self.private_runtime_root()
        archive_path = self._ensure_runtime_archive(on_status=on_status)

        _emit_status(on_status, "Preparing embedded Python 3.12 runtime...")
        with tempfile.TemporaryDirectory(prefix="infengine-runtime-") as temp_dir:
            extract_root = os.path.join(temp_dir, "python312")
            os.makedirs(extract_root, exist_ok=True)
            with zipfile.ZipFile(archive_path, "r") as zf:
                zf.extractall(extract_root)

            shutil.rmtree(runtime_root, ignore_errors=True)
            shutil.copytree(extract_root, runtime_root)

        python_exe = _find_python_in_root(runtime_root)
        if not python_exe or not _is_python312(python_exe):
            raise PythonRuntimeError(
                "Embedded Python 3.12 was extracted, but python.exe was not detected afterwards."
            )

        self._prepare_embedded_runtime(python_exe, on_status=on_status)
        return python_exe

    def _prepare_embedded_runtime(self, python_exe: str, *, on_status: Optional[Callable[[str], None]] = None) -> None:
        runtime_root = os.path.dirname(python_exe)
        _enable_site_for_embedded_runtime(runtime_root)
        self._ensure_pip(python_exe, on_status=on_status)
        self._ensure_runtime_packages(python_exe, on_status=on_status)

    def _ensure_runtime_archive(self, *, on_status: Optional[Callable[[str], None]] = None) -> str:
        archive_path = self.runtime_archive_path()
        _archive_name, archive_url = _runtime_embed_info_for_machine()
        os.makedirs(self.installed_runtime_dir(), exist_ok=True)

        if os.path.isfile(archive_path):
            return archive_path

        for candidate in self.bundled_archive_paths():
            if os.path.isfile(candidate):
                shutil.copy2(candidate, archive_path)
                return archive_path

        _emit_status(on_status, f"Downloading embedded Python 3.12 for {platform.machine()}...")
        tmp_path = archive_path + ".tmp"
        try:
            _download_file(
                archive_url,
                tmp_path,
                user_agent="InfEngine-Hub/1.0",
            )
        except urllib.error.URLError as exc:
            if "unknown url type: https" in str(exc).lower():
                raise PythonRuntimeError(
                    "Failed to download embedded Python 3.12 because HTTPS support is unavailable in the packaged Hub."
                ) from exc
            raise PythonRuntimeError(f"Failed to download embedded Python 3.12.\n{exc}") from exc
        except OSError as exc:
            raise PythonRuntimeError(f"Failed to download embedded Python 3.12.\n{exc}") from exc

        os.replace(tmp_path, archive_path)
        return archive_path

    def _get_pip_script_path(self, *, on_status: Optional[Callable[[str], None]] = None) -> str:
        target_path = os.path.join(self.installed_runtime_dir(), "get-pip.py")
        if os.path.isfile(target_path):
            return target_path

        for root in self.bundled_runtime_dirs():
            candidate = os.path.join(root, "get-pip.py")
            if os.path.isfile(candidate):
                shutil.copy2(candidate, target_path)
                return target_path

        _emit_status(on_status, "Downloading pip bootstrap...")
        try:
            _download_file(
                "https://bootstrap.pypa.io/get-pip.py",
                target_path,
                user_agent="InfEngine-Hub/1.0",
            )
        except urllib.error.URLError as exc:
            raise PythonRuntimeError(f"Failed to download get-pip.py.\n{exc}") from exc
        except OSError as exc:
            raise PythonRuntimeError(f"Failed to download get-pip.py.\n{exc}") from exc
        return target_path

    def _wheelhouse_dirs(self) -> list[str]:
        candidates = [
            os.path.join(self.installed_runtime_dir(), "wheels"),
            str(_RUNTIME_ROOT / "wheels"),
        ]
        if not is_frozen():
            candidates.insert(1, os.path.join(_legacy_runtime_dir(), "wheels"))
        for root in self.bundled_runtime_dirs():
            candidates.append(os.path.join(root, "wheels"))
        result: list[str] = []
        seen: set[str] = set()
        for path in candidates:
            if not path or not os.path.isdir(path):
                continue
            norm = os.path.normcase(os.path.abspath(path))
            if norm in seen:
                continue
            seen.add(norm)
            result.append(path)
        return result

    def _ensure_pip(self, python_exe: str, *, on_status: Optional[Callable[[str], None]] = None) -> None:
        completed = _run_command([python_exe, "-m", "pip", "--version"], timeout=60, raise_on_error=False)
        if completed.returncode == 0:
            return

        get_pip_path = self._get_pip_script_path(on_status=on_status)
        _emit_status(on_status, "Installing pip into the embedded runtime...")
        completed = _run_command(
            [python_exe, get_pip_path, "--no-warn-script-location"],
            timeout=1800,
            raise_on_error=False,
        )
        if completed.returncode != 0:
            raise PythonRuntimeError(
                "Failed to install pip into the embedded Python runtime.\n"
                f"{(completed.stderr or completed.stdout or '').strip()}"
            )

    def _ensure_runtime_packages(self, python_exe: str, *, on_status: Optional[Callable[[str], None]] = None) -> None:
        if self._has_modules(python_exe, "pip", "virtualenv", "nuitka", "PIL", "imageio", "av"):
            return

        _emit_status(on_status, "Installing embedded runtime support packages...")
        args = [
            python_exe,
            "-m",
            "pip",
            "install",
            "--disable-pip-version-check",
            "--no-input",
            "--prefer-binary",
            "--upgrade",
            "--target",
            _site_packages_root(os.path.dirname(python_exe)),
        ]
        wheelhouse_dirs = self._wheelhouse_dirs()
        if wheelhouse_dirs:
            args.append("--no-index")
            for wheelhouse in wheelhouse_dirs:
                args.extend(["--find-links", wheelhouse])

        args.extend(_RUNTIME_PACKAGES)
        completed = _run_command(args, timeout=1800, raise_on_error=False)
        if completed.returncode != 0:
            raise PythonRuntimeError(
                "Failed to install support packages into the embedded Python runtime.\n"
                f"{(completed.stderr or completed.stdout or '').strip()}"
            )

        if not self._has_modules(python_exe, "pip", "virtualenv", "nuitka", "PIL", "imageio", "av"):
            raise PythonRuntimeError(
                "Embedded Python runtime is still missing required support packages after installation."
            )

    def _has_modules(self, python_exe: str, *module_names: str) -> bool:
        checks = " and ".join(
            [f"importlib.util.find_spec('{module_name}') is not None" for module_name in module_names]
        )
        completed = _run_command(
            [python_exe, "-c", f"import importlib.util; print(int({checks}))"],
            timeout=30,
            raise_on_error=False,
        )
        return completed.returncode == 0 and (completed.stdout or "").strip() == "1"

    def _create_runtime_env(self, python_exe: str, target_root: str) -> None:
        commands: list[list[str]] = []
        if self._has_modules(python_exe, "virtualenv"):
            commands.append([python_exe, "-m", "virtualenv", "--always-copy", target_root])
        if not _is_embedded_root(os.path.dirname(python_exe)):
            commands.append([python_exe, "-m", "venv", "--copies", target_root])

        if not commands:
            raise PythonRuntimeError(
                "The managed Python runtime cannot create virtual environments because virtualenv is unavailable."
            )

        last_error = ""
        for args in commands:
            completed = _run_command(args, timeout=600, raise_on_error=False)
            if completed.returncode == 0:
                return
            last_error = (completed.stderr or completed.stdout or "").strip()

        raise PythonRuntimeError(
            "Failed to prepare the reusable virtual environment template.\n"
            f"{last_error}"
        )

    def _install_template_packages(self, venv_root: str, *, on_status: Optional[Callable[[str], None]] = None) -> None:
        if sys.platform == "win32":
            venv_python = os.path.join(venv_root, "Scripts", "python.exe")
        else:
            venv_python = os.path.join(venv_root, "bin", "python")

        if not os.path.isfile(venv_python):
            raise PythonRuntimeError(
                f"Virtual environment template python was not found at {venv_python}."
            )

        _emit_status(on_status, "Installing build packages into the reusable venv template...")
        args = [
            venv_python,
            "-m",
            "pip",
            "install",
            "--disable-pip-version-check",
            "--no-input",
            "--prefer-binary",
        ]
        wheelhouse_dirs = self._wheelhouse_dirs()
        if wheelhouse_dirs:
            args.append("--no-index")
            for wheelhouse in wheelhouse_dirs:
                args.extend(["--find-links", wheelhouse])

        args.extend(_TEMPLATE_BUILDER_PACKAGES)
        completed = _run_command(args, timeout=1800, raise_on_error=False)
        if completed.returncode != 0:
            raise PythonRuntimeError(
                "Failed to install build support packages into the reusable venv template.\n"
                f"{(completed.stderr or completed.stdout or '').strip()}"
            )

        if not self._has_modules(venv_python, "nuitka", "PIL", "imageio", "av"):
            raise PythonRuntimeError(
                "The reusable venv template is missing required build packages after installation."
            )

    def _ensure_venv_template(self, python_exe: str, *, on_status: Optional[Callable[[str], None]] = None) -> None:
        if self.has_venv_template():
            return

        template_root = self.venv_template_root()
        os.makedirs(os.path.dirname(template_root), exist_ok=True)
        temp_root = template_root + ".tmp"
        shutil.rmtree(temp_root, ignore_errors=True)
        shutil.rmtree(template_root, ignore_errors=True)

        _emit_status(on_status, "Preparing reusable virtual environment template...")
        self._create_runtime_env(python_exe, temp_root)
        self._install_template_packages(temp_root, on_status=on_status)
        self._rewrite_pyvenv_cfg(temp_root, python_exe)
        os.replace(temp_root, template_root)

    def _rewrite_pyvenv_cfg(self, venv_root: str, base_python: str) -> None:
        cfg_path = os.path.join(venv_root, "pyvenv.cfg")
        base_home = os.path.dirname(base_python)
        version = self._get_python_version(base_python)
        command = f'"{base_python}" -m virtualenv --always-copy "{venv_root}"'

        with open(cfg_path, "w", encoding="utf-8") as f:
            f.write(f"home = {base_home}\n")
            f.write("include-system-site-packages = false\n")
            f.write(f"version = {version}\n")
            f.write(f"executable = {base_python}\n")
            f.write(f"command = {command}\n")

    def _get_python_version(self, python_exe: str) -> str:
        completed = _run_command(
            [python_exe, "-c", "import sys; print(f'{sys.version_info.major}.{sys.version_info.minor}.{sys.version_info.micro}')"],
            timeout=20,
            raise_on_error=False,
        )
        if completed.returncode != 0:
            raise PythonRuntimeError(
                f"Failed to query the Python version for {python_exe}.\n{(completed.stderr or completed.stdout or '').strip()}"
            )
        return (completed.stdout or "").strip()

    def _is_valid_venv(self, venv_root: str) -> bool:
        cfg_path = os.path.join(venv_root, "pyvenv.cfg")
        if sys.platform == "win32":
            venv_python = os.path.join(venv_root, "Scripts", "python.exe")
        else:
            venv_python = os.path.join(venv_root, "bin", "python")
        return os.path.isfile(cfg_path) and os.path.isfile(venv_python)