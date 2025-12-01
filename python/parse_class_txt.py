import re
import sys
from pathlib import Path
from collections import defaultdict
import win32clipboard as clip


def copy_to_clipboard_unicode(text: str):
    """Copy text to Windows clipboard as Unicode text."""
    clip.OpenClipboard()
    clip.EmptyClipboard()
    clip.SetClipboardText(text, clip.CF_UNICODETEXT)
    clip.CloseClipboard()


def _format_left_to_right(var_name: str, tail: str) -> str:
    """
    Evaluate an expression that starts with var_name and then a sequence of
    operator/operand tokens, left-to-right, with division truncating via INT.
    Example:
      var_name = "PLAYER_LEVEL"
      tail = " / 30 + 1 * 22"
    Produces:
      (INT(PLAYER_LEVEL / 30) + 1) * 22
    Note: result has no leading '='.
    """
    # normalize whitespace and split into tokens (operators and operands)
    tail = tail.strip()
    if not tail:
        return var_name

    # Tokenize: find sequence of (op, operand)
    tokens = re.findall(r"([+\-*/])\s*([A-Za-z0-9_]+)", tail)
    prev = var_name
    for op, operand in tokens:
        # Operand may be numeric or variable name; keep as-is
        if op == "/":
            # truncating division: INT(prev / operand)
            prev = f"INT({prev} / {operand})"
        else:
            # left-associative: wrap previous and apply op
            prev = f"({prev} {op} {operand})"
    return prev


def parse_dice_average(dice_str: str, parts: dict):
    """
    Return (expr, mode)
      - expr: expression WITHOUT a leading '='
      - mode: one of 'multiplier' (meant to be multiplied with a base_expr),
              'standalone' (complete RHS expression),
              'concat' (string concatenation; standalone)
    This version supports many formats and uses left-to-right arithmetic with INT on divisions.
    """
    if not dice_str:
        return "", "standalone"

    s = re.sub(r"\s+", "", dice_str)  # remove whitespace for pattern matching

    # simple integer: 2000 -> standalone "2000"
    if re.fullmatch(r"\d+", s):
        return s, "standalone"

    # NdM or Nd$S (like 3d6, 1d4, 3d$S)
    m = re.fullmatch(r"(\d*)d(\$[A-Z]|\d+)", s)
    if m:
        count = int(m.group(1) or 1)
        sides = m.group(2)
        if sides.startswith("$"):
            var = sides[1:]
            if var in parts:
                sides_expr = (
                    f"INT({parts[var][1]})"
                    if isinstance(parts[var], tuple)
                    else f"INT({parts[var]})"
                )
            else:
                sides_expr = var
            return f"{count}*(({sides_expr}+1)/2)", "standalone"
        else:
            return f"{count}*(({sides}+1)/2)", "standalone"

    # $DdN -> multiplier relative to D (D provides count). Return multiplier only.
    m = re.fullmatch(r"\$Dd(\d+)", s)
    if m:
        sides = m.group(1)
        return f"(({sides}+1)/2)", "multiplier"

    # $Dd$S -> multiplier relative to D, sides come from S expr
    if re.fullmatch(r"\$Dd\$S", s):
        if "S" in parts:
            # parts['S'] is stored as (var, expr_tail) tuple in convert_avgdmg (see below)
            s_var, s_fullexpr = parts["S"]
            # build left-to-right for the S expression then wrap with INT(...) if it contains '/'
            s_expr_ltr = (
                _format_left_to_right(s_var, s_fullexpr[len(s_var) :])
                if s_fullexpr.startswith(s_var)
                else s_fullexpr
            )
            return f"((INT({s_expr_ltr})+1)/2)", "multiplier"
        else:
            return f"((S+1)/2)", "multiplier"

    # M$M -> string concatenation: ="M" & <Mexpr>
    if re.fullmatch(r"M\$M", s):
        if "M" in parts:
            m_var, m_fullexpr = parts["M"]
            # format full M expression as left-to-right (no leading '=') and don't INT-wrap generically
            m_expr_ltr = (
                _format_left_to_right(m_var, m_fullexpr[len(m_var) :])
                if m_fullexpr.startswith(m_var)
                else m_fullexpr
            )
            return f'"M" & {m_expr_ltr}', "concat"
        else:
            return f'"M" & M', "concat"

    # $B+m$M -> concatenation using B and M exprs
    if re.fullmatch(r"\$B\+m\$M", s):
        if "B" in parts:
            b_var, b_full = parts["B"]
            b_expr_ltr = (
                _format_left_to_right(b_var, b_full[len(b_var) :])
                if b_full.startswith(b_var)
                else b_full
            )
            b_wrapped = f"INT({b_expr_ltr})"
        else:
            b_wrapped = "INT(B)"
        if "M" in parts:
            m_var, m_full = parts["M"]
            m_expr_ltr = (
                _format_left_to_right(m_var, m_full[len(m_var) :])
                if m_full.startswith(m_var)
                else m_full
            )
            m_wrapped = f"INT({m_expr_ltr})"
        else:
            m_wrapped = "INT(M)"
        return f'{b_wrapped} & "+m" & {m_wrapped}', "concat"

    # $B -> standalone INT(Bexpr)
    if re.fullmatch(r"\$B", s):
        if "B" in parts:
            b_var, b_full = parts["B"]
            b_expr_ltr = (
                _format_left_to_right(b_var, b_full[len(b_var) :])
                if b_full.startswith(b_var)
                else b_full
            )
            return f"INT({b_expr_ltr})", "standalone"
        else:
            return "INT(B)", "standalone"

    # 20+d20 -> =20+((20+1)/2)
    m = re.fullmatch(r"(\d+)\+d(\d+)", s)
    if m:
        pre, sides = m.groups()
        return f"{pre}+(({sides}+1)/2)", "standalone"

    # 5+d$S -> =5+((INT(Sexpr)+1)/2)
    m = re.fullmatch(r"(\d+)\+d\$(\w+)", s)
    if m:
        pre, var = m.groups()
        if var in parts:
            var_name, var_full = parts[var]
            var_ltr = (
                _format_left_to_right(var_name, var_full[len(var_name) :])
                if var_full.startswith(var_name)
                else var_full
            )
            return f"{pre}+((INT({var_ltr})+1)/2)", "standalone"
        else:
            return f"{pre}+((INT({var})+1)/2)", "standalone"

    # $B+d20 -> =<Bexpr>+((20+1)/2)
    m = re.fullmatch(r"\$B\+d(\d+)", s)
    if m:
        sides = m.group(1)
        if "B" in parts:
            b_var, b_full = parts["B"]
            b_ltr = (
                _format_left_to_right(b_var, b_full[len(b_var) :])
                if b_full.startswith(b_var)
                else b_full
            )
            return f"{b_ltr}+(({sides}+1)/2)", "standalone"
        else:
            return f"B+(({sides}+1)/2)", "standalone"

    # 20+1d30 -> generalized form (N + NdM)
    m = re.fullmatch(r"(\d+)\+(\d*)d(\d+)", s)
    if m:
        pre, cnt, sides = m.groups()
        cnt = int(cnt or 1)
        return f"{pre}+({cnt}*(({sides}+1)/2))", "standalone"

    # $B+3d6 or $B+1d8 etc.
    m = re.fullmatch(r"\$B\+(\d*)d(\d+)", s)
    if m:
        count = int(m.group(1) or 1)
        sides = int(m.group(2))
        if "B" in parts:
            b_var, b_full = parts["B"]
            b_ltr = (
                _format_left_to_right(b_var, b_full[len(b_var) :])
                if b_full.startswith(b_var)
                else b_full
            )
            b_wrapped = f"INT({b_ltr})"
        else:
            b_wrapped = "INT(B)"
        return f"{b_wrapped}+{count}*((({sides}+1)/2))", "standalone"

    # --- $B + d$S ---
    m = re.fullmatch(r"\$B\+d\$(\w+)", s)
    if m:
        s_var = m.group(1)  # e.g. "S"
        # Format B part
        if "B" in parts:
            b_var, b_full = parts["B"]
            b_ltr = (
                _format_left_to_right(b_var, b_full[len(b_var) :])
                if b_full.startswith(b_var)
                else b_full
            )
            b_wrapped = f"INT({b_ltr})"
        else:
            b_wrapped = "INT(B)"
        # Format S part
        if s_var in parts:
            s_var_name, s_full = parts[s_var]
            s_ltr = (
                _format_left_to_right(s_var_name, s_full[len(s_var_name) :])
                if s_full.startswith(s_var_name)
                else s_full
            )
            s_wrapped = f"INT({s_ltr})"
        else:
            s_wrapped = f"INT({s_var})"
        return f"{b_wrapped}+(({s_wrapped}+1)/2)", "standalone"

    # --- $<prefix> (like $S) -> standalone INT(expr) ---
    m = re.fullmatch(r"\$(\w+)", s)
    if m:
        var_prefix = m.group(1)
        if var_prefix in parts:
            var_name, var_full = parts[var_prefix]
            var_ltr = (
                _format_left_to_right(var_name, var_full[len(var_name) :])
                if var_full.startswith(var_name)
                else var_full
            )
            return f"INT({var_ltr})", "standalone"
        else:
            return f"INT({var_prefix})", "standalone"

    # Fallback: return raw (standalone)
    return s, "standalone"


def convert_avgdmg(exprs: list[str], dice: str | None) -> str:
    """
    Combine expr list and dice into Excel-like AvgDmg formula (string, with leading '=').
    - exprs: list of raw expr lines like "D:PLAYER_LEVEL:/ 30 + 1 * 22"
    - dice: the dice string such as "$Dd4", "3d$S", "5+1d5", etc.
    """
    # Build parts mapping: prefix -> (var_name, full_expr_string)
    # full_expr_string is the expression as parsed from the file, e.g. "PLAYER_LEVEL / 30 + 1 * 22"
    parts = {}
    for raw in exprs:
        raw = raw.strip()
        m = re.match(r"^([DMBSC]):([A-Z_]+):(.+)$", raw)
        if m:
            prefix, var, expr = m.groups()
            expr = re.sub(r"\s+", " ", expr.strip())
            if not expr.startswith(var):
                expr = f"{var} {expr}"
            parts[prefix] = (var, expr)

    # Choose base key priority D, M, B
    base_key = None
    for k in ("D", "M", "B"):
        if k in parts:
            base_key = k
            break

    avg_expr, mode = parse_dice_average(dice or "", parts)

    # If mode == 'multiplier' and base_key present -> multiply base (formatted LTR) by multiplier
    if base_key and mode == "multiplier":
        var_name, full = parts[base_key]
        base_ltr = (
            _format_left_to_right(var_name, full[len(var_name) :])
            if full.startswith(var_name)
            else full
        )
        # base should be used wrapped by INT if it involves division at first op? We'll always INT-wrap the base when used as count
        # to match prior expectations (e.g. =INT(<expr>) * multiplier)
        base_wrapped = f"INT({base_ltr})"
        return f"={base_wrapped}*{avg_expr}"

    # If concat -> return formula
    if mode == "concat":
        return f"={avg_expr}"

    # If mode == 'standalone' and base_key exists:
    # some dice forms like 3d$S produce standalone average and should not multiply by base.
    # But forms like $B+d20 should produce <Bexpr> + ((20+1)/2) — parsed earlier in parse_dice_average to standalone.
    if mode == "standalone" and base_key:
        # If parse_dice_average returned an expression that already includes the base (e.g. "$B+d20 handled),
        # just return it. Otherwise, prefer to return standalone avg expression.
        # We detect whether avg_expr references the base variable by name.
        var_name, full = parts[base_key]
        if var_name in avg_expr:
            return f"={avg_expr}"
        # otherwise return avg_expr as full formula
        return f"={avg_expr}"

    # If only avg_expr present (no base), return it
    if avg_expr:
        return f"={avg_expr}"

    # Only base present and no dice -> return INT(base_ltr)
    if base_key:
        var_name, full = parts[base_key]
        base_ltr = (
            _format_left_to_right(var_name, full[len(var_name) :])
            if full.startswith(var_name)
            else full
        )
        return f"=INT({base_ltr})"

    return ""


def parse_class_txt(path="class.txt"):
    classes = []
    current = None
    current_book = None
    current_spell = None

    def add_current():
        nonlocal current
        if current:
            classes.append(current)
            current = None

    with open(path, encoding="utf-8") as f:
        for line in f:
            line = line.strip()
            if not line or line.startswith("#"):
                continue

            key, *rest = line.split(":", 1)
            value = rest[0].strip() if rest else ""

            # === Class header ===
            if key == "name":
                add_current()
                current = defaultdict(list)
                current["name"] = value
                current["books"] = []
                current_book = None
                current_spell = None

            # === Books ===
            elif key == "book":
                parts = value.split(":")
                while len(parts) < 5:
                    parts.append("")
                _, quality, name, spells, realm = parts[-5:]
                current_book = {"name": name.strip("[]"), "spells": []}
                current["books"].append(current_book)

            elif key == "spell":
                parts = value.split(":")
                spell_name, level, mana, fail, exp = (parts + ["", "", "", "", ""])[:5]
                current_spell = {
                    "letter": chr(96 + (len(current_book["spells"]) + 1)),
                    "name": spell_name,
                    "level": int(level or 0),
                    "mana": int(mana or 0),
                    "fail": int(fail or 0),
                    "exp": int(exp or 0),
                    "exprs": [],
                    "desc": "",
                }
                if current_book:
                    current_book["spells"].append(current_spell)

            elif key == "effect" and current_spell:
                current_spell["effect"] = value
            elif key == "dice" and current_spell:
                current_spell["dice"] = value
            elif key == "expr" and current_spell:
                current_spell["exprs"].append(value)
            elif key == "desc" and current_spell:
                current_spell["desc"] += (
                    " " if current_spell["desc"] else ""
                ) + value.strip()

    add_current()

    # Post-process avg dmg
    for c in classes:
        for b in c["books"]:
            for s in b["spells"]:
                exprs = s.get("exprs", [])
                dice = s.get("dice", "") or ""
                s["avgdmg"] = convert_avgdmg(exprs, dice)
                s["expr_combined"] = ", ".join(exprs)

    return classes


def build_tsv(classes):
    header = [
        "Class",
        "Book",
        "Letter",
        "Spell",
        "Level",
        "Mana",
        "Fail%",
        "Exp",
        "Effect",
        "AvgDmg",
        "Expr",
        "Dice",
        "Description",
    ]
    rows = ["\t".join(header)]

    for c in classes:
        cname = c.get("name", "")
        for b in c.get("books", []):
            bname = f"[{b.get('name', '')}]"
            for s in b.get("spells", []):
                dice_str = s.get("dice", "") or ""
                # prefix dice column with a single quote to avoid currency auto-conversion
                if dice_str and not dice_str.startswith("'"):
                    dice_out = "'" + dice_str
                else:
                    dice_out = dice_str
                row = [
                    cname,
                    bname,
                    f"{s.get('letter', '')})",
                    s.get("name", ""),
                    str(s.get("level", "")),
                    str(s.get("mana", "")),
                    str(s.get("fail", "")),
                    str(s.get("exp", "")),
                    s.get("effect", ""),
                    s.get("avgdmg", ""),
                    s.get("expr_combined", ""),
                    dice_out,
                    s.get("desc", "").replace("\t", " ").replace("\n", " "),
                ]
                rows.append("\t".join(row))
    return "\n".join(rows)


if __name__ == "__main__":
    path = Path("lib/gamedata/class.txt")
    if not path.exists():
        print(f"ERROR: Input file not found: {path}", file=sys.stderr)
        sys.exit(1)

    classes = parse_class_txt(path)
    tsv = build_tsv(classes)
    copy_to_clipboard_unicode(tsv)
    print("✅ TSV copied to clipboard (UnicodeText). Rows:", tsv.count("\n"))
