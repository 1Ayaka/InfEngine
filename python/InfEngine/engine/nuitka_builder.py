"""
NuitkaBuilder — compiles a Python entry script into a standalone native EXE
using Nuitka (Python → C → native binary).

This replaces the old RuntimeBuilder cache-copy approach with true native
compilation.  The output is a self-contained directory containing the EXE,
all required DLLs, and the embedded Python runtime.

MinGW64 is auto-downloaded by Nuitka so no manual compiler install is needed.
To avoid MinGW's std::filesystem crash on non-ASCII user paths (e.g. Chinese
usernames), all intermediate compilation is done in an ASCII-safe staging
directory and moved to the final destination afterwards.
"""

from __future__ import annotations

import hashlib
import os
import shutil
import subprocess
import sys
import threading
from pathlib import Path
from typing import Callable, List, Optional

from InfEngine.debug import Debug

# ASCII-safe root for Nuitka staging (avoids MinGW std::filesystem crash
# on non-ASCII characters in TEMP / user-profile paths).
_STAGING_ROOT = "C:\\_InfBuild"

# Persistent Nuitka compilation cache — lives outside the per-build staging
# directory so it survives across builds, dramatically speeding up rebuilds.
_NUITKA_CACHE_DIR = os.path.join(_STAGING_ROOT, "_nuitka_cache")


class _BuildCancelled(Exception):
    """Raised when the user cancels the build."""


class NuitkaBuilder:
    """Wraps Nuitka compilation for InfEngine standalone builds."""

    def __init__(
        self,
        entry_script: str,
        output_dir: str,
        *,
        output_filename: str = "Game.exe",
        product_name: str = "InfEngine Game",
        file_version: str = "1.0.0.0",
        icon_path: Optional[str] = None,
        extra_include_packages: Optional[List[str]] = None,
        extra_include_data: Optional[List[str]] = None,
        console_mode: str = "disable",
    ):
        self.entry_script = os.path.abspath(entry_script)
        self.output_dir = os.path.abspath(output_dir)
        self.output_filename = output_filename
        self.product_name = product_name
        self.file_version = file_version
        self.icon_path = icon_path
        self.console_mode = console_mode
        self.extra_include_packages = list(extra_include_packages or [])
        self.extra_include_data = list(extra_include_data or [])

        # Staging directory — unique per build to allow parallel builds
        tag = hashlib.md5(self.output_dir.encode()).hexdigest()[:8]
        self._staging_dir = os.path.join(_STAGING_ROOT, tag)

    # ------------------------------------------------------------------
    # Public API
    # ------------------------------------------------------------------

    def build(
        self,
        on_progress: Optional[Callable[[str, float], None]] = None,
        cancel_event: Optional[threading.Event] = None,
    ) -> str:
        """Run Nuitka compilation.  Returns the dist directory path."""

        def _p(msg: str, pct: float):
            if cancel_event is not None and cancel_event.is_set():
                raise _BuildCancelled()
            if on_progress:
                on_progress(msg, pct)
            Debug.log_internal(f"[NuitkaBuilder {pct:.0%}] {msg}")

        _p("检查 Nuitka 可用性 Checking Nuitka...", 0.0)
        self._check_nuitka()

        _p("准备暂存目录 Preparing staging directory...", 0.03)
        self._prepare_staging()

        _p("构建 Nuitka 命令 Building command...", 0.05)
        cmd = self._build_command()
        _p(f"命令: {' '.join(cmd)}", 0.05)

        _p("执行 Nuitka 编译 Running Nuitka compilation...", 0.10)
        dist_dir = self._run_nuitka(cmd, on_progress, cancel_event)

        _p("注入原生引擎库 Injecting native engine libraries...", 0.85)
        self._inject_native_libs(dist_dir)

        if sys.platform == "win32":
            _p("嵌入 UTF-8 清单 Embedding UTF-8 manifest...", 0.90)
            self._embed_utf8_manifest(dist_dir)

            _p("签名可执行文件 Signing executable...", 0.92)
            self._sign_executable(dist_dir)

        _p("清理编译产物 Cleaning build artifacts...", 0.95)
        self._cleanup_build_artifacts()

        _p("Nuitka 编译完成 Compilation complete!", 1.0)
        return dist_dir

    # ------------------------------------------------------------------
    # Nuitka availability check
    # ------------------------------------------------------------------

    @staticmethod
    def _check_nuitka():
        """Ensure Nuitka is installed; auto-install if missing."""
        try:
            import nuitka  # noqa: F401
        except ImportError:
            Debug.log_internal("Nuitka not found — installing automatically...")
            subprocess.check_call(
                [sys.executable, "-m", "pip", "install",
                 "nuitka", "ordered-set", "--quiet"],
            )
            # Verify the install succeeded
            try:
                import nuitka  # noqa: F401
            except ImportError:
                raise RuntimeError(
                    "Failed to auto-install Nuitka.  "
                    "Please run manually:\n    pip install nuitka ordered-set"
                )

    # ------------------------------------------------------------------
    # Staging directory (ASCII-safe for MinGW compatibility)
    # ------------------------------------------------------------------

    def _prepare_staging(self):
        """Create a clean ASCII-only staging directory.

        MinGW's ``std::filesystem`` implementation crashes when paths
        contain non-ASCII characters (e.g. Chinese usernames). By
        running Nuitka inside an ASCII-only staging dir we sidestep
        the issue entirely.
        """
        if os.path.isdir(self._staging_dir):
            shutil.rmtree(self._staging_dir, ignore_errors=True)
        os.makedirs(self._staging_dir, exist_ok=True)

        # Copy entry script into staging (path itself may be non-ASCII)
        staged_script = os.path.join(self._staging_dir, "boot.py")
        shutil.copy2(self.entry_script, staged_script)
        self._staged_entry = staged_script

    # ------------------------------------------------------------------
    # Command construction
    # ------------------------------------------------------------------

    def _build_command(self) -> List[str]:
        """Assemble the Nuitka command line.

        All output paths point to the ASCII-safe staging directory.
        """
        cmd = [
            sys.executable, "-m", "nuitka",
            "--standalone",
            "--assume-yes-for-downloads",
            f"--windows-console-mode={self.console_mode}",
            "--follow-imports",
            f"--output-dir={self._staging_dir}",
            f"--output-filename={self.output_filename}",
            # Disable Nuitka's deployment-time hard-crash when an excluded
            # module is imported.  Some modules are legitimately excluded
            # but lazily imported with graceful fallback (try/except or
            # None checks); the default deployment flag converts those
            # into RuntimeErrors which is counter-productive.
            "--no-deployment-flag=excluded-module-usage",
        ]

        # On Windows, prefer MSVC which produces binaries that are far less
        # likely to trigger antivirus false positives compared to MinGW.
        # MSVC also handles Unicode paths natively, avoiding the
        # std::filesystem crashes that plague MinGW on non-ASCII paths.
        # Falls back to MinGW (auto-downloaded by Nuitka) when MSVC is
        # unavailable.
        if sys.platform == "win32":
            if shutil.which("cl") or os.path.exists(
                os.path.join(
                    os.environ.get("ProgramFiles", ""),
                    "Microsoft Visual Studio",
                )
            ):
                cmd.append("--msvc=latest")
            else:
                cmd.append("--mingw64")

        # Link-time optimization for smaller and faster binaries
        cmd.append("--lto=yes")

        # Parallel C compilation
        cmd.append("--jobs=%d" % max(1, os.cpu_count() - 1))

        # Include package data (fonts, shaders, icons…) but NOT the whole
        # package as source — let --follow-imports trace only what the
        # player entry script actually needs.  This avoids compiling the
        # entire editor UI (hundreds of files) which is never used.
        cmd += [
            "--include-package-data=InfEngine",
        ]

        # Explicitly ensure the pybind11 native extension is bundled
        # (Nuitka may not auto-detect it because it's a .pyd, not .py).
        cmd.append("--include-module=InfEngine.lib._InfEngine")

        # Prevent Nuitka from following into editor-only modules that the
        # standalone player never uses.  The _INFENGINE_PLAYER_MODE guard
        # in __init__ already prevents runtime loading, but --nofollow
        # also speeds up Nuitka's compile-time analysis significantly.
        #
        # NOTE: Do NOT exclude InfEngine.engine.resources_manager here —
        # render_stack.py lazily imports ResourcesManager.instance() and
        # Nuitka's excluded-module deployment flag causes a hard crash
        # instead of allowing the graceful None fallback.
        for _editor_mod in (
            "InfEngine.engine.bootstrap",
            "watchdog",
            "PIL",
            "cv2",
            "imageio",
            "psd_tools",
        ):
            cmd.append(f"--nofollow-import-to={_editor_mod}")

        for pkg in self.extra_include_packages:
            cmd.append(f"--include-package={pkg}")

        for pattern in self.extra_include_data:
            cmd.append(f"--include-package-data={pattern}")

        # Product metadata (Windows)
        if sys.platform == "win32":
            cmd.append(f"--product-name={self.product_name}")
            cmd.append(f"--file-version={self.file_version}")
            cmd.append(f"--product-version={self.file_version}")

            if self.icon_path and os.path.isfile(self.icon_path):
                ico = self._ensure_ico(self.icon_path)
                if ico:
                    cmd.append(f"--windows-icon-from-ico={ico}")

        # Exclude heavy dev/test modules that aren't needed at runtime
        for mod in ("tkinter", "unittest", "test", "pip",
                    "setuptools", "distutils", "ensurepip"):
            cmd.append(f"--nofollow-import-to={mod}")

        cmd.append(self._staged_entry)
        return cmd

    # ------------------------------------------------------------------
    # Nuitka execution
    # ------------------------------------------------------------------

    def _run_nuitka(
        self,
        cmd: List[str],
        on_progress: Optional[Callable[[str, float], None]],
        cancel_event: Optional[threading.Event] = None,
    ) -> str:
        """Run Nuitka as a subprocess and stream output.  Returns dist dir."""
        env = os.environ.copy()

        # Redirect TEMP / TMP to an ASCII-safe location so MinGW's
        # std::filesystem never encounters non-ASCII characters.
        safe_tmp = os.path.join(self._staging_dir, "_tmp")
        os.makedirs(safe_tmp, exist_ok=True)
        env["TEMP"] = safe_tmp
        env["TMP"] = safe_tmp
        # Use a persistent cache directory so Nuitka can reuse compiled C
        # code across builds — this is the single biggest speed win.
        os.makedirs(_NUITKA_CACHE_DIR, exist_ok=True)
        env["NUITKA_CACHE_DIR"] = _NUITKA_CACHE_DIR

        proc = subprocess.Popen(
            cmd,
            stdout=subprocess.PIPE,
            stderr=subprocess.STDOUT,
            text=True,
            encoding="utf-8",
            errors="replace",
            env=env,
            cwd=self._staging_dir,
        )

        lines_collected: List[str] = []
        try:
            for line in proc.stdout:
                if cancel_event is not None and cancel_event.is_set():
                    raise _BuildCancelled()
                line = line.rstrip()
                lines_collected.append(line)
                if on_progress:
                    # Crude progress: Nuitka logs many lines; we map to 10%–85%
                    pct = min(0.85, 0.10 + len(lines_collected) * 0.001)
                    on_progress(line[-80:] if len(line) > 80 else line, pct)
        except _BuildCancelled:
            proc.kill()
            proc.wait()
            raise

        proc.wait()

        if proc.returncode != 0:
            tail = "\n".join(lines_collected[-30:])
            raise RuntimeError(
                f"Nuitka compilation failed (exit code {proc.returncode}).\n"
                f"Last output:\n{tail}"
            )

        # Nuitka places output in <staging_dir>/boot.dist/
        dist_dir = os.path.join(self._staging_dir, "boot.dist")
        if not os.path.isdir(dist_dir):
            raise RuntimeError(
                f"Nuitka dist directory not found: {dist_dir}\n"
                "Compilation may have failed silently."
            )
        return dist_dir

    # ------------------------------------------------------------------
    # Inject native engine libraries
    # ------------------------------------------------------------------

    def _inject_native_libs(self, dist_dir: str):
        """Copy _InfEngine.pyd + engine DLLs into the Nuitka dist directory.

        Nuitka won't automatically pick up .pyd files built outside its
        compilation scope (pybind11 extensions), so we inject them into
        the correct package subdirectory so that
        ``from ._InfEngine import *`` (relative import in InfEngine.lib)
        can find the .pyd, and ``os.add_dll_directory(lib_dir)`` picks
        up the companion DLLs.
        """
        import InfEngine.lib as _lib
        lib_dir = Path(_lib.__file__).parent

        # Target: <dist>/InfEngine/lib/  — mirrors the installed package
        # structure so relative imports work at runtime.
        target_dir = Path(dist_dir) / "InfEngine" / "lib"
        target_dir.mkdir(parents=True, exist_ok=True)

        # Also put DLLs in the dist root as a fallback for Windows DLL
        # search (the .exe directory is always searched).
        dist_root = Path(dist_dir)

        # List of native files to inject
        native_files = []
        for f in lib_dir.iterdir():
            if f.is_file() and f.suffix.lower() in (".pyd", ".dll"):
                native_files.append(f)

        for src in native_files:
            # .pyd goes into the package subdir (for relative import)
            dst_pkg = target_dir / src.name
            if not dst_pkg.exists():
                shutil.copy2(src, dst_pkg)
                Debug.log_internal(f"  Injected (lib): {src.name}")

            # DLLs also go into the dist root (for OS DLL search path)
            if src.suffix.lower() == ".dll":
                dst_root = dist_root / src.name
                if not dst_root.exists():
                    shutil.copy2(src, dst_root)
                    Debug.log_internal(f"  Injected (root): {src.name}")

    # ------------------------------------------------------------------
    # UTF-8 application manifest (Windows)
    # ------------------------------------------------------------------

    # Complete manifest that tells Windows to use UTF-8 as the process's
    # ANSI code page (Windows 10 1903+).  Without this, any path
    # containing non-ASCII characters (e.g. Chinese usernames) causes
    # the C++ engine to fail with "No mapping for the Unicode character
    # exists in the target multi-byte code page".
    _UTF8_MANIFEST = (
        b'<?xml version="1.0" encoding="UTF-8" standalone="yes"?>\r\n'
        b'<assembly xmlns="urn:schemas-microsoft-com:asm.v1" manifestVersion="1.0">\r\n'
        b'  <trustInfo xmlns="urn:schemas-microsoft-com:asm.v3">\r\n'
        b'    <security>\r\n'
        b'      <requestedPrivileges>\r\n'
        b'        <requestedExecutionLevel level="asInvoker" uiAccess="false"/>\r\n'
        b'      </requestedPrivileges>\r\n'
        b'    </security>\r\n'
        b'  </trustInfo>\r\n'
        b'  <compatibility xmlns="urn:schemas-microsoft-com:compatibility.v1">\r\n'
        b'    <application>\r\n'
        b'      <supportedOS Id="{8e0f7a12-bfb3-4fe8-b9a5-48fd50a15a9a}"/>\r\n'
        b'    </application>\r\n'
        b'  </compatibility>\r\n'
        b'  <application xmlns="urn:schemas-microsoft-com:asm.v3">\r\n'
        b'    <windowsSettings>\r\n'
        b'      <activeCodePage xmlns="http://schemas.microsoft.com/SMI/2019/WindowsSettings">UTF-8</activeCodePage>\r\n'
        b'      <dpiAware xmlns="http://schemas.microsoft.com/SMI/2005/WindowsSettings">true/pm</dpiAware>\r\n'
        b'      <dpiAwareness xmlns="http://schemas.microsoft.com/SMI/2016/WindowsSettings">permonitorv2,permonitor</dpiAwareness>\r\n'
        b'    </windowsSettings>\r\n'
        b'  </application>\r\n'
        b'</assembly>\r\n'
    )

    def _embed_utf8_manifest(self, dist_dir: str):
        """Embed an application manifest with UTF-8 active code page.

        Uses the Win32 resource-update API so no external tools (mt.exe,
        rc.exe) are required.  Replaces the default Nuitka manifest.
        """
        import ctypes
        from ctypes import wintypes

        exe_path = os.path.join(dist_dir, self.output_filename)
        if not os.path.isfile(exe_path):
            Debug.log_warning(
                f"Cannot embed manifest: EXE not found at {exe_path}"
            )
            return

        k32 = ctypes.windll.kernel32

        # --- open for resource update --------------------------------
        k32.BeginUpdateResourceW.argtypes = [wintypes.LPCWSTR, wintypes.BOOL]
        k32.BeginUpdateResourceW.restype = wintypes.HANDLE
        h = k32.BeginUpdateResourceW(exe_path, False)
        if not h:
            Debug.log_warning(
                f"BeginUpdateResource failed (error {ctypes.GetLastError()})"
            )
            return

        # RT_MANIFEST = 24, CREATEPROCESS_MANIFEST_RESOURCE_ID = 1
        RT_MANIFEST = 24
        MANIFEST_ID = 1
        data = self._UTF8_MANIFEST

        k32.UpdateResourceW.argtypes = [
            wintypes.HANDLE,   # hUpdate
            wintypes.LPVOID,   # lpType  (MAKEINTRESOURCE)
            wintypes.LPVOID,   # lpName  (MAKEINTRESOURCE)
            wintypes.WORD,     # wLanguage
            ctypes.c_char_p,   # lpData
            wintypes.DWORD,    # cb
        ]
        k32.UpdateResourceW.restype = wintypes.BOOL

        ok = k32.UpdateResourceW(h, RT_MANIFEST, MANIFEST_ID, 0, data, len(data))
        if not ok:
            Debug.log_warning(
                f"UpdateResource failed (error {ctypes.GetLastError()})"
            )
            k32.EndUpdateResourceW(h, True)  # discard changes
            return

        k32.EndUpdateResourceW.argtypes = [wintypes.HANDLE, wintypes.BOOL]
        k32.EndUpdateResourceW.restype = wintypes.BOOL
        k32.EndUpdateResourceW(h, False)

        Debug.log_internal("Embedded UTF-8 active-code-page manifest")

    # ------------------------------------------------------------------
    # Code signing (reduces antivirus false positives)
    # ------------------------------------------------------------------

    def _sign_executable(self, dist_dir: str):
        """Sign the built EXE with a self-signed certificate.

        Unsigned executables — especially those compiled with MinGW —
        are far more likely to trigger antivirus false positives because
        they lack an Authenticode signature.  This method creates a
        self-signed code-signing certificate (cached per-machine) and
        applies it to the output EXE using PowerShell's
        ``Set-AuthenticodeSignature``.

        A self-signed certificate won't prevent SmartScreen warnings
        (that requires a purchased EV certificate), but it does help
        with heuristic-based AV scanners that penalise unsigned binaries.
        """
        exe_path = os.path.join(dist_dir, self.output_filename)
        if not os.path.isfile(exe_path):
            return

        # Use PowerShell to: (1) find or create a self-signed code signing
        # cert in CurrentUser\\My, (2) sign the EXE.
        ps_script = r'''
$certName = "InfEngine Build Signing"
$cert = Get-ChildItem Cert:\CurrentUser\My -CodeSigningCert |
        Where-Object { $_.Subject -eq "CN=$certName" -and $_.NotAfter -gt (Get-Date) } |
        Select-Object -First 1

if (-not $cert) {
    $cert = New-SelfSignedCertificate `
        -Subject "CN=$certName" `
        -Type CodeSigningCert `
        -CertStoreLocation Cert:\CurrentUser\My `
        -NotAfter (Get-Date).AddYears(5)
}

$result = Set-AuthenticodeSignature -FilePath $EXE_PATH -Certificate $cert -HashAlgorithm SHA256
$result.Status
'''
        ps_script = ps_script.replace("$EXE_PATH", f'"{exe_path}"')
        try:
            r = subprocess.run(
                ["powershell", "-NoProfile", "-ExecutionPolicy", "Bypass",
                 "-Command", ps_script],
                capture_output=True, text=True, timeout=30,
            )
            output = (r.stdout or "").strip()
            if "Valid" in output:
                Debug.log_internal("Signed EXE with self-signed certificate")
            else:
                Debug.log_warning(
                    f"Code signing returned: {output or r.stderr.strip()}"
                )
        except Exception as exc:
            Debug.log_warning(f"Code signing skipped: {exc}")

    # ------------------------------------------------------------------
    # Cleanup
    # ------------------------------------------------------------------

    def _cleanup_build_artifacts(self):
        """Remove Nuitka's intermediate .build directory from staging."""
        build_dir = os.path.join(self._staging_dir, "boot.build")
        if os.path.isdir(build_dir):
            shutil.rmtree(build_dir, ignore_errors=True)
        # Also remove the staging temp dir
        safe_tmp = os.path.join(self._staging_dir, "_tmp")
        if os.path.isdir(safe_tmp):
            shutil.rmtree(safe_tmp, ignore_errors=True)
        # Remove the copied boot script
        staged_script = os.path.join(self._staging_dir, "boot.py")
        if os.path.isfile(staged_script):
            os.remove(staged_script)

    # ------------------------------------------------------------------
    # Icon conversion
    # ------------------------------------------------------------------

    def _ensure_ico(self, icon_path: str) -> Optional[str]:
        """Return a .ico path, converting from PNG/JPG if needed.

        Nuitka's ``--windows-icon-from-ico`` requires a real .ico file.
        If the source is already .ico, return it as-is.  Otherwise
        convert via Pillow (no ImageMagick needed).
        """
        ext = os.path.splitext(icon_path)[1].lower()
        if ext == ".ico":
            return icon_path

        try:
            from PIL import Image
        except ImportError:
            Debug.log_warning(
                "Pillow not installed — skipping icon embedding.  "
                "Install with: pip install Pillow"
            )
            return None

        ico_path = os.path.join(self._staging_dir, "icon.ico")
        try:
            img = Image.open(icon_path)
            # Standard Windows icon sizes
            sizes = [(256, 256), (128, 128), (64, 64), (48, 48), (32, 32), (16, 16)]
            img.save(ico_path, format="ICO", sizes=sizes)
            Debug.log_internal(f"Converted {os.path.basename(icon_path)} → icon.ico")
            return ico_path
        except Exception as exc:
            Debug.log_warning(f"Icon conversion failed: {exc}")
            return None
