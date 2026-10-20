"""Wraps UI string literals in tr() (client localization) and extracts every translatable key.

python tools/i18n_wrap.py wrap     # rewrites src/client/*.cpp in place (idempotent)
python tools/i18n_wrap.py extract  # writes data/lang/keys.json: every UI literal plus game data texts
Literals are wrapped when they are whole arguments of ImGui text/widget calls or of our widgets, contain letters and
are not pure ImGui ids ("##x"). Keys drop leading Font Awesome icons/spaces and a trailing "##id", like tr() does at runtime.
"""
import glob
import json
import os
import re
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
FILES = ["Ui.cpp", "UiCampaign.cpp", "UiDebug.cpp", "UiDesigner.cpp", "UiDesktop.cpp", "UiDock.cpp", "UiGroups.cpp", "UiGuide.cpp",
         "UiMenu.cpp", "App.cpp", "Editor.cpp", "Sound.cpp"]
IMGUI = ["Text", "TextDisabled", "TextColored", "TextWrapped", "TextUnformatted", "BulletText", "Button", "SmallButton", "Selectable",
         "Checkbox", "SliderInt", "SliderFloat", "Combo", "BeginCombo", "MenuItem", "BeginMenu", "BeginTabItem", "SeparatorText",
         "SetItemTooltip", "SetTooltip", "InputText", "InputTextWithHint", "CollapsingHeader", "TreeNode", "RadioButton", "Begin",
         "BeginPopupModal", "TableSetupColumn", "LabelText", "InputInt", "DragInt", "DragFloat", "InputFloat"]
OWN = ["accentButton", "toggleButton", "menuButton", "centeredText", "cardButton", "cardSelectable", "showBanner", "unitTooltip", "bar",
       "hotkeyRow", "statLine"]
LITERAL = re.compile(r'^\s*((ICON_FA_\w+)\s*)*("(?:\\.|[^"\\])*"\s*)+$')
STR = re.compile(r'"((?:\\.|[^"\\])*)"')


def split_args(text, start):
    """text[start] is just after '('; returns (args [(begin, end)], index after ')')."""
    depth, i, begin, args = 0, start, start, []
    while i < len(text):
        c = text[i]
        if c == '"':
            i += 1
            while text[i] != '"':
                i += 2 if text[i] == '\\' else 1
        elif c == "'":
            i += 1
            while text[i] != "'":
                i += 2 if text[i] == '\\' else 1
        elif c in "([{":
            depth += 1
        elif c in ")]}":
            if depth == 0:
                args.append((begin, i))
                return args, i + 1
            depth -= 1
        elif c == ',' and depth == 0:
            args.append((begin, i))
            begin = i + 1
        i += 1
    return args, i


def literal_text(arg):
    return "".join(m.group(1) for m in STR.finditer(arg))


def wants(arg):
    if not LITERAL.match(arg):
        return False
    t = literal_text(arg)
    return not t.startswith("##") and re.search(r"[A-Za-z]{2,}", t.split("##")[0]) is not None


def wrap_file(path):
    text = open(path, encoding="utf-8").read()
    names = "|".join(["ImGui::" + f for f in IMGUI] + OWN)
    pattern = re.compile(r"(?<![\w:])(" + names + r")\(")
    out, pos, count = [], 0, 0
    for m in pattern.finditer(text):
        if m.start() < pos:
            continue
        line_start = text.rfind("\n", 0, m.start()) + 1
        prefix = text[line_start:m.start()]
        if re.match(r"^\s*(inline\s+)?(bool|void|static)\b", prefix) or "App::" in prefix:
            continue  # a definition, not a call
        args, end = split_args(text, m.end())
        chunk = text[m.start():end]
        new = text[m.start():m.end()]
        cursor = m.end()
        for b, e in args:
            new += text[cursor:b]
            arg = text[b:e]
            stripped = arg.strip()
            if wants(stripped) and not stripped.startswith("tr("):
                lead = arg[:len(arg) - len(arg.lstrip())]
                trail = arg[len(arg.rstrip()):]
                new += lead + "tr(" + stripped + ")" + trail
                count += 1
            else:
                new += arg
            cursor = e
        new += text[cursor:end]
        out.append(text[pos:m.start()])
        out.append(new)
        pos = end
    out.append(text[pos:])
    result = "".join(out)
    if result != text:
        open(path, "w", encoding="utf-8", newline="\n").write(result)
    return count


ICON = re.compile(r"^(?:\s|\\xef\\x[89][0-9a-f]\\x[0-9a-f]{2})*")


def key_of(t):
    t = t.split("##")[0].strip()
    return t.encode().decode("unicode_escape") if "\\" in t else t


def extract():
    keys = {}

    def add(t, where):
        t = t.strip()
        if t and re.search(r"[A-Za-zА-Яа-я]{2,}", t):
            keys.setdefault(t, where)

    for f in FILES:
        text = open(os.path.join(ROOT, "src", "client", f), encoding="utf-8").read()
        for m in re.finditer(r"(?<![\w:])tr\(", text):
            args, _ = split_args(text, m.end())
            if args:
                arg = text[args[0][0]:args[0][1]].strip()
                if LITERAL.match(arg):
                    add(key_of(literal_text(arg)), f)
        for m in re.finditer(r"(?<![\w])(k(?:ColorNames|DifficultyNames|WindowModes|FpsNames|Aa|Shadows|Lights|Layouts|Ranks|ToolNames|CellNames|Branches))(?![\w])[^=;]*=\s*\{", text):
            end = text.index("};", m.end())
            for lit in STR.finditer(text[m.end():end]):
                add(key_of(lit.group(1)), f)
    data = os.path.join(ROOT, "data")
    units = json.load(open(os.path.join(data, "units.json"), encoding="utf-8"))
    for u in units["units"]:
        add(u.get("name", ""), "units")
    for k in ("bodies", "propulsion", "weapons"):
        for p in units.get("design", {}).get(k, []):
            add(p.get("name", ""), "design")
    for t in json.load(open(os.path.join(data, "research.json"), encoding="utf-8"))["topics"]:
        add(t.get("name", ""), "research")
        add(t.get("desc", ""), "research")
    for s in json.load(open(os.path.join(data, "skills.json"), encoding="utf-8"))["skills"]:
        add(s.get("name", ""), "skills")
    items = json.load(open(os.path.join(data, "items.json"), encoding="utf-8"))
    for i in items.get("items", []) + items.get("modules", []):
        add(i.get("name", ""), "items")
    cat = json.load(open(os.path.join(data, "maps", "maps.json"), encoding="utf-8"))
    for m in cat["maps"] + cat["modes"]:
        add(m.get("name", ""), "maps")
        add(m.get("description", ""), "maps")
    camp = json.load(open(os.path.join(data, "campaign", "campaign.json"), encoding="utf-8"))
    for ch in camp["chapters"]:
        add(ch.get("name", ""), "campaign")
        add(ch.get("intro", ""), "campaign")
        for m in ch["missions"]:
            for k in ("name", "place", "debrief"):
                add(m.get(k, ""), "campaign")
            mp = json.load(open(os.path.join(data, "maps", m["map"] + ".json"), encoding="utf-8"))
            mi = mp.get("mission", {})
            add(mi.get("title", ""), "mission")
            add(mi.get("briefing", ""), "mission")
            for o in mi.get("objectives", []):
                add(o.get("text", ""), "mission")
            for t in mi.get("triggers", []):
                for a in t.get("do", []):
                    add(a.get("text", ""), "mission")
    for f in glob.glob(os.path.join(ROOT, "src", "server", "*.cpp")):
        text = open(f, encoding="utf-8").read()
        for m in re.finditer(r'notice\(\s*("(?:\\.|[^"\\])*")', text):
            add(key_of(m.group(1)[1:-1]), "server")
    out = os.path.join(ROOT, "tools", "lang_src", "keys.json")
    os.makedirs(os.path.dirname(out), exist_ok=True)
    json.dump(keys, open(out, "w", encoding="utf-8", newline="\n"), ensure_ascii=False, indent=1)
    print(len(keys), "keys ->", out)


if __name__ == "__main__":
    if sys.argv[1:] == ["wrap"]:
        total = 0
        for f in FILES:
            n = wrap_file(os.path.join(ROOT, "src", "client", f))
            print(f, n)
            total += n
        print("wrapped", total)
    else:
        extract()
