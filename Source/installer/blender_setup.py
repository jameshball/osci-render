"""Run by osci-installer inside Blender, never by the system Python.

Each action runs in a fresh process so migrating a legacy add-on with a broken
unregister() cannot leave classes behind in the installation process.
"""

import sys

# Setup must not create Python caches inside the Blender application bundle.
sys.dont_write_bytecode = True

import argparse
from contextlib import redirect_stdout, redirect_stderr
import io
import json
from pathlib import Path
import traceback

import addon_utils
import bpy

REPOSITORY_URL = "https://osci-render.com/blender/index.json"
REPOSITORY_ID = "osci_render"
PACKAGE_ID = "osci_render"


def require_finished(result, operation):
    if "FINISHED" not in result:
        raise RuntimeError(f"Blender could not {operation}")


def extension_operation(operator, **kwargs):
    # Blender 4.2 can return FINISHED even when its download subprocess failed.
    # Preserve the diagnostic and treat that failure as a setup error.
    output = io.StringIO()
    try:
        with redirect_stdout(output), redirect_stderr(output):
            result = operator(**kwargs)
    finally:
        print(output.getvalue(), end="", flush=True)
    errors = [line for line in output.getvalue().splitlines()
              if line.startswith(("FATAL_ERROR", "ERROR", "Error:"))]
    if errors:
        raise RuntimeError("\n".join(errors))
    require_finished(result, "complete the extension operation")


def repository(url):
    repos = bpy.context.preferences.extensions.repos
    matches = [repo for repo in repos if repo.remote_url.rstrip("/") == url.rstrip("/")]
    if len(matches) > 1:
        raise RuntimeError("Multiple osci-render repositories found; remove the duplicate in Blender Preferences")
    return matches[0] if matches else None


def module_name(repo):
    return f"bl_ext.{repo.module}.{PACKAGE_ID}"


def inspect(url):
    if bpy.app.version < (4, 2, 0):
        raise RuntimeError("Blender 4.2 or newer is required")
    repo = repository(url)
    enabled = list(bpy.context.preferences.addons.keys())
    target = module_name(repo) if repo else None
    conflicts = [name for name in enabled if name != target and
                 (name == PACKAGE_ID or (name.startswith("bl_ext.") and name.endswith("." + PACKAGE_ID)))]
    return {
        "version": bpy.app.version_string,
        "profile": bpy.utils.user_resource("CONFIG"),
        "repository": repo.module if repo else "",
        "enabled": target in enabled if target else False,
        "conflicts": conflicts,
    }


def prepare(args):
    state = inspect(args.url)
    if state["conflicts"] and not args.replace:
        raise RuntimeError("An older osci-render add-on is enabled. Select Replace existing integration to continue")
    repo = repository(args.url)
    if repo is None:
        repos = bpy.context.preferences.extensions.repos
        if any(item.module == REPOSITORY_ID for item in repos):
            raise RuntimeError("The osci_render repository name is used by another source; resolve it in Blender Preferences")
        repo = repos.new(name="osci-render", module=REPOSITORY_ID, remote_url=args.url)
    if not repo.enabled or repo.source != "USER":
        raise RuntimeError("Enable the osci-render user repository in Blender Preferences before installing")
    # Record only preferences this operation owns. Never restore a whole profile.
    journal = {"target": module_name(repo), "previously_enabled": state["enabled"],
               "disabled": state["conflicts"]}
    args.journal.write_text(json.dumps(journal), encoding="utf-8")
    for name in state["conflicts"]:
        # Do not call old unregister() implementations: some shipped versions fail.
        # This process exits before installing; the old files remain recoverable.
        bpy.context.preferences.addons.remove(bpy.context.preferences.addons[name])
    require_finished(bpy.ops.wm.save_userpref(), "save preferences")
    return state


def install(args):
    # Blender's extension subprocess otherwise writes .pyc files inside Blender.app,
    # triggering macOS App Management protection for the parent installer.
    from bl_pkg import bl_extension_utils
    command = bl_extension_utils.blender_ext_cmd
    bl_extension_utils.blender_ext_cmd = lambda python_args: command((*python_args, "-B"))
    repo = repository(args.url)
    if repo is None:
        raise RuntimeError("The osci-render repository was not saved")
    extension_operation(bpy.ops.extensions.repo_sync, repo_directory=repo.directory)
    extension_operation(bpy.ops.extensions.package_install, repo_directory=repo.directory,
                        pkg_id=PACKAGE_ID, enable_on_install=True)
    errors = []
    module = addon_utils.enable(module_name(repo), default_set=True,
                                handle_error=lambda: errors.append(traceback.format_exc()))
    if module is None or errors:
        raise RuntimeError("Extension activation failed: " + "\n".join(errors))
    require_finished(bpy.ops.wm.save_userpref(), "save extension activation")
    return inspect(args.url)


def verify(args):
    state = inspect(args.url)
    repo = repository(args.url)
    if not state["enabled"] or state["conflicts"] or repo is None:
        raise RuntimeError("Extension activation was not preserved; retry after closing Blender")
    if not addon_utils.check(module_name(repo))[1] or not hasattr(bpy.types.Scene, "oscirenderPort"):
        raise RuntimeError("The installed extension failed to load; see the setup log")
    return state


def rollback(args):
    if args.journal.exists():
        state = json.loads(args.journal.read_text(encoding="utf-8"))
        addons = bpy.context.preferences.addons
        if state["previously_enabled"] and state["target"] not in addons:
            addons.new().module = state["target"]
        elif not state["previously_enabled"] and state["target"] in addons:
            addons.remove(addons[state["target"]])
        for name in state["disabled"]:
            if name not in addons:
                addons.new().module = name
        require_finished(bpy.ops.wm.save_userpref(), "restore the previous add-on selection")
    return {"restored": True}


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("action", choices=["inspect", "prepare", "install", "verify", "rollback"])
    parser.add_argument("--url", default=REPOSITORY_URL)
    parser.add_argument("--journal", type=Path)
    parser.add_argument("--replace", action="store_true")
    args = parser.parse_args(sys.argv[sys.argv.index("--") + 1:])
    try:
        if bpy.app.version < (4, 2, 0):
            raise RuntimeError("Blender 4.2 or newer is required")
        result = inspect(args.url) if args.action == "inspect" else globals()[args.action](args)
        print("OSCI_RESULT=" + json.dumps({"ok": True, **result}), flush=True)
    except Exception as error:
        traceback.print_exc()
        print("OSCI_RESULT=" + json.dumps({"ok": False, "error": str(error)}), flush=True)
        raise


if __name__ == "__main__":
    main()
