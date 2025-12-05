import re
from pathlib import Path
import sys
import argparse


def parse_object_file(text):
    entries = []
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
                entry[current_key][-1] += " " + line.strip()
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
        out_lines.append("")
    return "\n".join(out_lines)


# --- NEW: Extract numeric cost and weight safely ---
def parse_int_safe(val):
    try:
        return int(val)
    except:
        # Handle random expressions like 1d3+5 → treat as base value only
        m = re.match(r"(\d+)", val)
        return int(m.group(1)) if m else 0


def compute_value_metrics(entry):
    cost = parse_int_safe(entry.get("cost", "0"))
    weight_tenths = parse_int_safe(entry.get("weight", "0"))
    weight_lbs = weight_tenths / 10 if weight_tenths else 0
    value_per_lb = cost / weight_lbs if weight_lbs > 0 else 0

    return cost, weight_lbs, value_per_lb


# --- NEW: Generate markdown table ---
def make_markdown_table(title, items):
    out = []
    out.append(f"## {title}\n")
    out.append("| Item | Cost | Weight (lb) | Value per lb |")
    out.append("|------|------|-------------|---------------|")

    for e, cost, weight, vpp in items:
        out.append(
            f"| {e.get('name','?')} | {cost} | {weight:.1f} | {vpp:.1f} |"
        )

    return "\n".join(out) + "\n"


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("file", nargs="?", default="lib/gamedata/object.txt")
    parser.add_argument("--markdown", action="store_true",
                        help="Output markdown tables for most valuable items")
    parser.add_argument("--top", type=int, default=20,
                        help="How many items to show in markdown tables")

    args = parser.parse_args()

    path = Path(args.file)
    text = path.read_text(encoding="utf-8", errors="ignore")
    entries = parse_object_file(text)

    if not args.markdown:
        print(to_yaml(entries))
        return

    # Compute metrics
    metrics = []
    for e in entries:
        cost, w, vpp = compute_value_metrics(e)
        metrics.append((e, cost, w, vpp))

    # Sort lists
    top_value = sorted(metrics, key=lambda t: t[1], reverse=True)[:args.top]
    top_vpp = sorted(metrics, key=lambda t: t[3], reverse=True)[:args.top]

    # Output markdown
    print(make_markdown_table("Highest Total Value", top_value))
    print(make_markdown_table("Highest Value per Pound", top_vpp))


if __name__ == "__main__":
    main()
