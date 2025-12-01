import re
import win32clipboard as clip

# Buff constants (modifiers applied to DangerScore)
BUFF_MODIFIERS = {
    "HAVE_BLESS": -2,
    "HAVE_HEROISM": -1,
    "HAVE_BERSERK": -1,
    "HAVE_HOLY_CHANT": -3,
    "HAVE_SLOW_MONSTER": -2,
}


# === Clipboard helper ===
def set_clipboard_text(text: str):
    clip.OpenClipboard()
    clip.EmptyClipboard()
    clip.SetClipboardText(text, clip.CF_UNICODETEXT)
    clip.CloseClipboard()


# === Parser ===
def parse_monsters(filename):
    monsters = []
    with open(filename, encoding="utf-8") as f:
        current = {}
        desc_lines = []
        for line in f:
            line = line.strip()
            if not line or line.startswith("#"):
                continue
            if line.startswith("name:"):
                if current:
                    current["desc"] = " ".join(desc_lines)
                    monsters.append(current)
                    desc_lines = []
                current = {"name": line.split(":", 1)[1].strip()}
            elif line.startswith("depth:"):
                current["depth"] = int(line.split(":", 1)[1])
            elif line.startswith("speed:"):
                current["speed"] = int(line.split(":", 1)[1])
            elif line.startswith("flags:"):
                current.setdefault("flags", []).extend(
                    re.split(r"\s*\|\s*", line.split(":", 1)[1])
                )
            elif line.startswith("desc:"):
                desc_lines.append(line[5:].strip())
        if current:
            current["desc"] = " ".join(desc_lines)
            monsters.append(current)
    return monsters


# === Generate TSV with formulas ===
def generate_clipboard_text(monsters):
    lines = []

    # Display buff variables above table
    lines.append("PLAYER_LEVEL\t=PLAYER_LEVEL")
    for buff in BUFF_MODIFIERS.keys():
        lines.append(f"{buff}\t={buff}")  # assume value comes from Google Sheets
    lines.append("SPEED_BONUS\t=SPEED_BONUS")
    lines.append("")  # blank line for spacing

    # Header
    header = [
        "Name",
        "Depth",
        "Speed",
        "DangerScore",
        "Verdict",
        "Flags",
        "Description",
    ]
    lines.append("\t".join(header))
    offset = len(lines)

    for i, m in enumerate(monsters, start=1):
        row = offset + i  # Excel row number
        flags_str = ", ".join(m.get("flags", []))

        # Effective monster speed relative to player
        formula = (
            f"=(B{row}-PLAYER_LEVEL)"  # depth difference
            f"+IF(C{row}-(110+SPEED_BONUS) > 0,"
            f"(C{row}-(110+SPEED_BONUS))/5,"
            f"(C{row}-(110+SPEED_BONUS))/10)"  # speed difference
            f'+IF(OR(ISNUMBER(SEARCH("BREATH",F{row})),'
            f'ISNUMBER(SEARCH("SUMMON",F{row})),'
            f'ISNUMBER(SEARCH("DRAIN",F{row})),'
            f'ISNUMBER(SEARCH("PARALYZE",F{row}))),5,0)'  # monster abilities
            f'+IF(ISNUMBER(SEARCH("UNIQUE",F{row})),3,0)'  # unique monsters
        )

        # Add buff modifiers
        for buff, mod in BUFF_MODIFIERS.items():
            formula += f"+IF({buff}, {mod}, 0)"

        # Verdict based on DangerScore
        verdict_formula = f'=IF(D{row}<0,"DOABLE",IF(D{row}<5,"RISKY","AVOID"))'

        line = [
            m.get("name", ""),
            str(m.get("depth", "")),
            str(m.get("speed", "")),
            formula,
            verdict_formula,
            flags_str,
            m.get("desc", ""),
        ]
        lines.append("\t".join(line))

    return "\n".join(lines)


# === Main ===
if __name__ == "__main__":
    monsters = parse_monsters("lib/gamedata/monster.txt")
    tsv_text = generate_clipboard_text(monsters)
    set_clipboard_text(tsv_text)
    print(f"Copied {len(monsters)} monsters + player info to clipboard with formulas.")
