#!/usr/bin/env python3
"""Audit every configured first-party C++ file with a host GCC toolchain.

Use a Release compilation database to check optimized code generation. This
preserves the configured macros/includes and excludes PCH and vendor units.
It supplements a native build; Windows audits do not reproduce Linux's ABI,
POSIX branches, sanitizers, or GPU drivers.
"""

import argparse
import concurrent.futures
import ctypes
import hashlib
import json
import os
from pathlib import Path
import re
import shlex
import shutil
import subprocess


def command_arguments(entry):
    if "arguments" in entry:
        return entry["arguments"]
    if os.name != "nt":
        return shlex.split(entry["command"])
    shell = ctypes.windll.shell32
    shell.CommandLineToArgvW.restype = ctypes.POINTER(ctypes.c_wchar_p)
    shell.CommandLineToArgvW.argtypes = [ctypes.c_wchar_p, ctypes.POINTER(ctypes.c_int)]
    count = ctypes.c_int()
    argv = shell.CommandLineToArgvW(entry["command"], ctypes.byref(count))
    if not argv:
        raise OSError("Could not parse compilation command")
    try:
        return [argv[index] for index in range(count.value)]
    finally:
        ctypes.windll.kernel32.LocalFree(argv)


def configured_arguments(entry):
    tokens = command_arguments(entry)[1:]
    arguments = []
    index = 0
    while index < len(tokens):
        token = tokens[index]
        if token in ("-D", "-I", "-isystem", "-iquote"):
            arguments.extend(tokens[index:index + 2])
            index += 2
            continue
        if token.startswith(("-D", "-I")):
            arguments.append(token)
        elif token.startswith(("/D", "/I")):
            arguments.append("-" + token[1:])
        elif token.startswith("-external:I"):
            arguments.extend(["-isystem", token[len("-external:I"):]])
        index += 1
    return arguments


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--database", required=True, type=Path)
    parser.add_argument("--compiler", default="g++")
    parser.add_argument("--tidy", help="Run all configured clang-tidy checks using GCC's headers")
    parser.add_argument("--tidy-checks", help="Optional clang-tidy check adjustment for tool version differences")
    parser.add_argument("--sources-only", action="store_true", help="Check src/tools, excluding tests")
    parser.add_argument("--output", type=Path, default=Path("out/compiler-warnings"))
    parser.add_argument("--jobs", type=int, default=2)
    args = parser.parse_args()
    if args.jobs < 1:
        parser.error("--jobs must be positive")
    compiler = shutil.which(args.compiler)
    if compiler is None:
        parser.error(f"Compiler not found: {args.compiler}")
    compiler = str(Path(compiler).resolve())
    root = Path(__file__).resolve().parents[1]
    output = args.output.resolve()
    output.mkdir(parents=True, exist_ok=True)
    excluded = {"MiniaudioImpl.cpp", "CgltfImplementation.cpp",
                "StbImageImplementation.cpp", "StbTruetypeImplementation.cpp",
                "VulkanMemoryAllocatorImplementation.cpp"}
    units = []
    for entry in json.loads(args.database.read_text(encoding="utf-8")):
        source = Path(entry["file"])
        if not source.is_absolute():
            source = Path(entry["directory"]) / source
        source = source.resolve()
        if not source.is_relative_to(root):
            continue
        relative = source.relative_to(root)
        if (relative.parts[0] not in (("src", "tools") if args.sources_only else ("src", "tools", "tests")) or
                source.suffix != ".cpp" or source.name in excluded):
            continue
        flags = configured_arguments(entry)
        if not args.tidy and "-DNDEBUG" not in flags:
            parser.error("Use a Release compilation database; a Debug database "
                         "would check different feature macros")
        key = hashlib.sha256(str(flags).encode()).hexdigest()[:10]
        units.append((entry, source, relative.as_posix().replace("/", "_") + "_" + key, flags))
    if not units:
        parser.error("No first-party C++ translation units found")
    env = dict(os.environ)
    env["PATH"] = str(Path(compiler).parent) + os.pathsep + env.get("PATH", "")
    version = subprocess.run([compiler, "--version"], check=True,
                             capture_output=True, text=True).stdout.splitlines()[0]
    tidy_arguments = None
    if args.tidy:
        tidy = shutil.which(args.tidy)
        if tidy is None:
            parser.error(f"clang-tidy not found: {args.tidy}")
        tidy = str(Path(tidy).resolve())
        target = subprocess.run([compiler, "-dumpmachine"], check=True,
                                capture_output=True, text=True).stdout.strip()
        headers = subprocess.run([compiler, "-E", "-x", "c++", "-", "-v"],
                                 input="", capture_output=True, text=True, check=True)
        directories = headers.stderr.split("#include <...> search starts here:", 1)[1].split("End of search list.", 1)[0]
        tidy_arguments = ["--target=" + target, "-std=c++20", "-Wall", "-Wextra",
                          "-Wpedantic", "-Wno-missing-field-initializers"]
        builtin_directories = {
            Path(subprocess.run([compiler, "-print-file-name=" + name], check=True,
                                capture_output=True, text=True).stdout.strip()).resolve()
            for name in ("include", "include-fixed")
        }
        for directory in directories.splitlines():
            # GCC's intrinsic headers use GCC-specific builtins. Clang must
            # use its own resource headers while sharing the C++/platform STL.
            if directory.strip() and Path(directory.strip()).resolve() not in builtin_directories:
                tidy_arguments.extend(["-isystem", directory.strip()])
        filter_pattern = "^" + re.escape(root.as_posix()).replace("/", r"[/\\]") + r"[/\\](src|tools)[/\\]"
        tidy_command = [tidy, "--config-file=" + str(root / ".clang-tidy"),
                        "--header-filter=" + filter_pattern, "--warnings-as-errors=*"]
        if args.tidy_checks:
            tidy_command.append("--checks=" + args.tidy_checks)
        tidy_version = subprocess.run([tidy, "--version"], check=True,
                                      capture_output=True, text=True).stdout.strip()
        print(tidy_version, flush=True)
    mode = "full clang-tidy policy" if args.tidy else "-O3 -Werror object compilation"
    print(f"{version}; host {os.name}; {len(units)} translation units; {mode}", flush=True)

    def compile_unit(unit):
        entry, source, name, flags = unit
        if args.tidy:
            command = [*tidy_command, str(source), "--", *tidy_arguments, *flags]
        else:
            command = [compiler, "-std=c++20", "-O3", "-Wall", "-Wextra", "-Wpedantic",
                       "-Wno-missing-field-initializers", "-Werror", *flags,
                       "-c", str(source), "-o", str(output / (name + ".o"))]
        result = subprocess.run(command, cwd=entry["directory"], env=env,
                                capture_output=True, text=True, errors="replace")
        (output / (name + ".log")).write_text(
            result.stdout + result.stderr, encoding="utf-8")
        return str(source.relative_to(root)), result.returncode

    failed = []
    with concurrent.futures.ThreadPoolExecutor(max_workers=args.jobs) as pool:
        futures = [pool.submit(compile_unit, unit) for unit in units]
        for completed, future in enumerate(concurrent.futures.as_completed(futures), 1):
            name, code = future.result()
            if code:
                failed.append(name)
                print(f"FAILED {name}", flush=True)
            if completed % 20 == 0 or completed == len(units):
                print(f"Checked {completed}/{len(units)}; {len(failed)} failed", flush=True)
    (output / "failures.json").write_text(json.dumps(failed, indent=2), encoding="utf-8")
    return 1 if failed else 0


if __name__ == "__main__":
    raise SystemExit(main())
