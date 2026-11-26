#!/usr/bin/env python3
"""Build a single-file Python CLI, without third-party packages."""
from pathlib import Path
import shutil
import tempfile
import zipapp

root = Path(__file__).resolve().parent
with tempfile.TemporaryDirectory(prefix="fucking-c-build-") as temporary:
    stage = Path(temporary)
    package = stage / "fucking_c"
    package.mkdir()
    for path in (root / "fucking_c").glob("*.py"):
        shutil.copy2(path, package / path.name)
    (stage / "__main__.py").write_text(
        "from fucking_c.cli import main\nraise SystemExit(main())\n", encoding="utf-8"
    )
    zipapp.create_archive(stage, root / "fc.pyz", interpreter="/usr/bin/env python3", compressed=True)
print("Готово: fc.pyz — один файл, Python 3.10+, без установки зависимостей.")
