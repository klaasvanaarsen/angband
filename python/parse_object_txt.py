import re
from pathlib import Path
import sys


def parse_object_file(text):
    entries = []
    # Split on "name:" that starts at the beginning of a line
    parts = re.split(r"(?=^name:)", text, flags=re.M)
    for part in parts:
        part = part.strip()
        if not part:
            continue
        entry = {}
        current_key = None
        for line in part.splitlines():
            if not line.strip() or line.strip().startswith("#"):
                continue
            m = re.match(r"^(\w[\w-]*):\s*(.*)$", line)
            if m:
                key, val = m.groups()
                # Start new multiline block for desc, flags, values, etc.
                if key in entry:
                    if isinstance(entry[key], list):
                        entry[key].append(val)
                    else:
                        entry[key] = [entry[key], val]
                elif key in {"desc", "flags", "values"}:
                    entry[key] = [val]
                else:
                    entry[key] = val
                current_key = key
            elif current_key in {"desc", "flags", "values"}:
                # Continuation of a previous multiline field
                entry[current_key][-1] += " " + line.strip()
        # Join multiline lists into strings where appropriate
        if "desc" in entry:
            entry["desc"] = " ".join(entry["desc"])
        entries.append(entry)
    return entries


def to_yaml(entries):
    out_lines = []
    for e in entries:
        out_lines.append("- name: " + e.get("name", ""))
        for k, v in e.items():
            if k == "name":
                continue
            if isinstance(v, list):
                out_lines.append(f"  {k}:")
                for item in v:
                    out_lines.append(f"    - {item}")
            else:
                out_lines.append(f"  {k}: {v}")
        out_lines.append("")  # blank line between entries
    return "\n".join(out_lines)


def main():
    path = Path(sys.argv[1] if len(sys.argv) > 1 else "lib/gamedata/object.txt")
    text = path.read_text(encoding="utf-8", errors="ignore")
    entries = parse_object_file(text)
    yaml_output = to_yaml(entries)
    print(yaml_output)


if __name__ == "__main__":
    main()
