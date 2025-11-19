import re
import csv

# Map dice symbols to typical die sides
DICE_SIDES = {
    "$Dd4": 4,
    "$Dd6": 6,
    "$Dd8": 8,
    "$Dd10": 10,
    "$Dd12": 12,
    "$B": 6,  # default for B, may be overridden
    "$S": 8,  # example
}


def parse_class_txt(filename):
    classes = {}
    current_class = None
    current_book = None
    letter_counter = ord("a")

    with open(filename, "r", encoding="utf-8") as f:
        lines = f.readlines()

    for line in lines:
        line = line.strip()
        if not line or line.startswith("#"):
            continue
        if line.startswith("name:"):
            # new class
            parts = line.split(":")
            current_class = parts[1].strip()
            classes[current_class] = []
            current_book = None
            letter_counter = ord("a")
        elif line.startswith("book:"):
            # new book
            parts = line.split(":")
            current_book = parts[3].strip()
            letter_counter = ord("a")
        elif line.startswith("spell:"):
            # spell line
            parts = line.split(":")
            spell_name = parts[1].strip()
            level = int(parts[2].strip())
            mana = int(parts[3].strip())
            fail = int(parts[4].strip())
            exp = parts[5].strip()
            spell_data = {
                "Class": current_class,
                "Book": current_book,
                "Letter": chr(letter_counter) + ")",
                "Spell": spell_name,
                "Level": level,
                "Mana": mana,
                "Fail%": fail,
                "Exp": exp,
                "Dice": "",
                "Effect": "",
                "Radius": "",
                "Description": "",
                "Expr": "",
                "AvgDmg": "",
            }
            classes[current_class].append(spell_data)
            letter_counter += 1
        elif line.startswith("dice:"):
            dice = line.split(":", 1)[1].strip()
            classes[current_class][-1]["Dice"] = dice
        elif line.startswith("effect:"):
            parts = line.split(":")
            classes[current_class][-1]["Effect"] = parts[1].strip()
            if len(parts) > 3:
                classes[current_class][-1]["Radius"] = parts[3].strip()
        elif line.startswith("expr:"):
            classes[current_class][-1]["Expr"] = line.split(":", 1)[1].strip()

    return classes


def expr_to_formula(expr, dice):
    # convert expr like D:PLAYER_LEVEL:- 1 / 5 + 3
    m = re.match(r"([A-Z]):(\w+):(.*)", expr)
    if not m:
        return ""
    code, base, operations = m.groups()
    operations = (
        operations.replace("/", "/")
        .replace("*", "*")
        .replace("+", "+")
        .replace("-", "-")
    )
    sides = DICE_SIDES.get(dice, 1)
    return f"=(( {base} {operations} )*(({sides}+1)/2))"


def write_csv(classes, filename):
    fieldnames = [
        "Class",
        "Book",
        "Letter",
        "Spell",
        "Level",
        "Mana",
        "Fail%",
        "Exp",
        "Dice",
        "Effect",
        "Radius",
        "Description",
        "Expr",
        "AvgDmg",
    ]
    with open(filename, "w", newline="", encoding="utf-8") as f:
        writer = csv.DictWriter(f, fieldnames=fieldnames)
        writer.writeheader()
        for class_name, spells in classes.items():
            for spell in spells:
                spell["AvgDmg"] = expr_to_formula(spell["Expr"], spell["Dice"])
                writer.writerow(spell)


if __name__ == "__main__":
    classes = parse_class_txt("lib/gamedata/class.txt")
    write_csv(classes, "spells_for_sheets.csv")
    print("CSV written to spells_for_sheets.csv")
