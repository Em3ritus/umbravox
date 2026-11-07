"""Keep compiler diagnostic strings and debug paths independent of the builder.

Applies to project, dependency and Arduino framework compilation. This changes
source-path metadata, not the paths used to locate input files at build time.
"""
from pathlib import Path


def prefix_flags(home, project):
    # GCC applies the last matching prefix mapping. Prefer the project mapping
    # over the enclosing home mapping so checkout names also stay private.
    mappings = [(str(Path(home).resolve()), "/build/user"),
                (str(Path(project).resolve()), "/src/umbravox")]
    return [f"-ffile-prefix-map={source}={target}"
            for source, target in mappings]


if "Import" in globals():
    Import("env")
    env.Append(CCFLAGS=prefix_flags(Path.home(), env.subst("$PROJECT_DIR")))
