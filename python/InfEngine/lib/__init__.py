import os
import sys

lib_dir = os.path.join(os.path.dirname(__file__))
lib_dir = os.path.abspath(lib_dir)

sys.path.insert(0, lib_dir)

if sys.platform == "win32":
    os.add_dll_directory(lib_dir)
    os.environ["PATH"] = lib_dir + ";" + os.environ["PATH"]
else:
    os.environ["LD_LIBRARY_PATH"] = lib_dir + ":" + os.environ.get("LD_LIBRARY_PATH", "")

from ._InfEngine import *