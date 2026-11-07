Import("env")
from pathlib import Path

components = Path(env["PROJECT_DIR"]).parent / "components"
for name in ("gg_core", "gg_app"):
    env.Append(CPPPATH=[str(components / name / "include")])
for name in ("gg_core", "gg_app"):
    env.BuildSources("$BUILD_DIR/" + name, str(components / name / "src"))
