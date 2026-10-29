#!/usr/bin/env python3
"""Print the -D flags a meson project accepts, corrected to its own types.

meson refuses to configure when handed an option the project does not declare,
and refuses again when the value does not fit the option's type. The option sets
of glib and pixman have both moved between releases - pixman gained cpu= after
0.43, glib gained man= after 2.74, and tests= is a boolean in one project and a
combo in the other - while this job deliberately pins versions that are not the
newest. Encoding which release introduced which option would be a table that
goes out of date on its own.

So read the project's declaration instead:

    meson setup build $(python3 tools/meson_opts.py . tests=false libpng=disabled cpu=base)

Undeclared options are dropped. Values are corrected to what the declared type
will accept, so a boolean gets true/false, a feature gets enabled/disabled, and
a combo gets one of its own listed choices. Every change is written to stderr,
because a build that silently lost -Dnls=disabled is a build that then fails on
msgfmt for a reason nobody can see in the log.

Nothing is invented: if a value cannot be mapped onto the declared choices the
option is dropped and said about, rather than passed through to fail the setup.
"""

import argparse
import re
import sys
from pathlib import Path

FILES = ("meson.options", "meson_options.txt")

START = re.compile(r"""option\(\s*['"]([^'"]+)['"]""")
TYPE = re.compile(r"""type\s*:\s*['"](\w+)['"]""")
CHOICES = re.compile(r"""choices\s*:\s*\[([^\]]*)\]""")
QUOTED = re.compile(r"""['"]([^'"]+)['"]""")

# Requested value -> what to try, in order, when the project's own vocabulary
# turns out to be different.
SYNONYMS = {
    "true": ["true", "enabled", "yes"],
    "false": ["false", "disabled", "no"],
    "enabled": ["enabled", "true", "auto"],
    "disabled": ["disabled", "false", "no"],
    "auto": ["auto"],
}


def declarations(project_dir):
    text = None
    for filename in FILES:
        path = Path(project_dir) / filename
        if path.is_file():
            text = path.read_text(encoding="utf-8", errors="replace")
            break
    if text is None:
        raise SystemExit("no meson option declaration file in %s" % project_dir)

    found = {}
    starts = list(START.finditer(text))
    for index, match in enumerate(starts):
        # Each declaration is read up to the start of the next one. Slicing on
        # the closing parenthesis is not safe: descriptions contain them.
        end = starts[index + 1].start() if index + 1 < len(starts) else len(text)
        body = text[match.end():end]
        type_match = TYPE.search(body)
        choices_match = CHOICES.search(body)
        found[match.group(1)] = {
            "type": type_match.group(1) if type_match else "boolean",
            "choices": QUOTED.findall(choices_match.group(1)) if choices_match else [],
        }
    return found


def correct(name, value, spec, project):
    kind = spec["type"]

    if kind in ("array", "string", "integer"):
        return "%s=%s" % (name, value)

    allowed = spec["choices"]
    if not allowed:
        # No explicit list, so the type carries it.
        allowed = {"boolean": ["true", "false"],
                   "feature": ["auto", "enabled", "disabled"],
                   "combo": []}[kind]

    if value in allowed:
        return "%s=%s" % (name, value)

    for candidate in SYNONYMS.get(value, []):
        if candidate in allowed:
            print("corrected %s=%s -> %s=%s (%s takes %s)"
                  % (name, value, name, candidate, kind, "|".join(allowed)),
                  file=sys.stderr)
            return "%s=%s" % (name, candidate)

    print("dropped %s=%s: %s is a %s accepting %s"
          % (name, value, name, kind, "|".join(allowed) or "nothing"),
          file=sys.stderr)
    return None


def main():
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("project", help="directory holding meson.options")
    ap.add_argument("option", nargs="+", help="name=value, without the -D")
    args = ap.parse_args()

    declared = declarations(args.project)
    flags = []
    for spec in args.option:
        name, _, value = spec.partition("=")
        if name not in declared:
            print("dropped %s: not declared by %s" % (name, args.project),
                  file=sys.stderr)
            continue
        if not value:
            print("dropped %s: no value given" % name, file=sys.stderr)
            continue
        fixed = correct(name, value, declared[name], args.project)
        if fixed:
            flags.append("-D" + fixed)

    print(" ".join(flags))
    return 0


if __name__ == "__main__":
    sys.exit(main())
