"""Builds data/lang/<code>.json from tools/lang_src/<code>.py (a list T aligned with tools/lang_src/keys_base.json plus EXTRA).

python tools/i18n_wrap.py extract && python tools/lang_build.py
Checks that every translation keeps the printf specifiers of its key in the same order.
"""
import importlib.util
import json
import os
import re
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
NAMES = {"ru": "Русский", "uk": "Українська", "de": "Deutsch", "es": "Español"}
SPEC = re.compile(r"%[-+#0]*(?:\d+|\*)?(?:\.(?:\d+|\*))?[diufsxXcp%]")


def main():
    # T lists are aligned with the frozen tools/lang_src/keys_base.json; keys added later go in each module's EXTRA dict
    keys = json.load(open(os.path.join(ROOT, "tools", "lang_src", "keys_base.json"), encoding="utf-8"))
    current = list(json.load(open(os.path.join(ROOT, "tools", "lang_src", "keys.json"), encoding="utf-8")))
    ok = True
    for code, name in NAMES.items():
        src = os.path.join(ROOT, "tools", "lang_src", code + ".py")
        if not os.path.exists(src):
            continue
        spec = importlib.util.spec_from_file_location("lang_" + code, src)
        mod = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(mod)
        T = mod.T
        if len(T) != len(keys):
            print(f"{code}: {len(T)} translations for {len(keys)} keys")
            ok = False
            continue
        strings = {}
        for i, (k, t) in enumerate(zip(keys, T)):
            if t is None:
                continue
            if SPEC.findall(k) != SPEC.findall(t):
                print(f"{code} #{i}: specifiers differ\n  {k!r}\n  {t!r}")
                ok = False
            strings[k] = t
        for k, t in getattr(mod, "EXTRA", {}).items():
            if SPEC.findall(k) != SPEC.findall(t):
                print(f"{code} extra: specifiers differ {k!r} {t!r}")
                ok = False
            strings[k] = t
        missing = [k for k in current if k not in strings]
        if missing:
            print(f"{code}: {len(missing)} untranslated, e.g. {missing[:5]}")
        out = os.path.join(ROOT, "data", "lang", code + ".json")
        with open(out, "w", encoding="utf-8", newline="\n") as f:
            json.dump({"name": name, "strings": strings}, f, ensure_ascii=False, indent=1)
        print(code, len(strings), "strings ->", out)
    sys.exit(0 if ok else 1)


if __name__ == "__main__":
    main()
