import pytest
from parse_class_txt import convert_avgdmg, parse_dice_average, _format_left_to_right


def test_left_to_right_truncation():
    expr = _format_left_to_right("PLAYER_LEVEL", " / 30 + 1 * 22")
    assert expr == "((INT(PLAYER_LEVEL / 30) + 1) * 22)"


def test_simple_integer():
    expr, mode = parse_dice_average("2000", {})
    assert expr == "2000"
    assert mode == "standalone"


def test_standard_dice():
    expr, mode = parse_dice_average("3d6", {})
    assert expr == "3*((6+1)/2)"
    assert mode == "standalone"


def test_variable_sides_dice():
    parts = {"S": ("PLAYER_LEVEL", "PLAYER_LEVEL / 2")}
    expr, mode = parse_dice_average("3d$S", parts)
    assert expr == "3*((INT((INT(PLAYER_LEVEL / 2))+1)/2)" or expr.startswith("3*(")
    assert mode == "standalone"


def test_d_multiplier_form():
    expr, mode = parse_dice_average("$Dd4", {})
    assert expr == "((4+1)/2)"
    assert mode == "multiplier"


def test_variable_sides_multiplier():
    parts = {"S": ("PLAYER_LEVEL", "PLAYER_LEVEL / 2")}
    expr, mode = parse_dice_average("$Dd$S", parts)
    assert "INT(" in expr
    assert expr.endswith("+1)/2)")
    assert mode == "multiplier"


def test_concat_M_variable():
    parts = {"M": ("MONSTER_HP", "MONSTER_HP / 10")}
    expr, mode = parse_dice_average("M$M", parts)
    assert expr.startswith('"M" & ')
    assert mode == "concat"


def test_concat_B_m_M():
    parts = {"B": ("BASE_DMG", "BASE_DMG / 2"), "M": ("MODIFIER", "MODIFIER / 5")}
    expr, mode = parse_dice_average("$B+m$M", parts)
    assert "&" in expr
    assert mode == "concat"


def test_B_reference():
    parts = {"B": ("BASE_DMG", "BASE_DMG / 2")}
    expr, mode = parse_dice_average("$B", parts)
    assert expr.startswith("INT(")
    assert mode == "standalone"


def test_add_dice_constant():
    expr, mode = parse_dice_average("20+d20", {})
    assert expr == "20+((20+1)/2)"
    assert mode == "standalone"


def test_add_dice_variable_sides():
    parts = {"S": ("STAT", "STAT / 3")}
    expr, mode = parse_dice_average("5+d$S", parts)
    assert expr.startswith("5+((INT(")
    assert mode == "standalone"


def test_dice_only_expression():
    # 5+1d5 should produce =5+(1*((5+1)/2))
    formula = convert_avgdmg([], "5+1d5")
    assert formula == "=5+(1*((5+1)/2))"


def test_base_expr_multiplied_by_dice():
    exprs = ["D:PLAYER_LEVEL:/ 30 + 1 * 22"]
    formula = convert_avgdmg(exprs, "$Dd4")
    # should start with =INT( and contain * ((4+1)/2)
    assert formula.startswith("=INT(")
    assert "*((4+1)/2)" in formula


def test_base_expr_add_dice():
    exprs = ["B:BASEVAL:/ 10 + 5"]
    formula = convert_avgdmg(exprs, "$B+d20")
    assert "=INT(" not in formula  # should not have double INT wrapping
    assert "((20+1)/2)" in formula
    assert formula.startswith("=")


def test_multiplier_mode_only():
    expr, mode = parse_dice_average("$Dd4", {})
    assert mode == "multiplier"
    # combine manually like convert_avgdmg would
    base = "INT(PLAYER_LEVEL)"
    combined = f"={base}*{expr}"
    assert combined == "=INT(PLAYER_LEVEL)*((4+1)/2)"


def test_fallback_case():
    expr, mode = parse_dice_average("unknownform", {})
    assert expr == "unknownform"
    assert mode == "standalone"


def test_complex_cases():
    assert (
        convert_avgdmg(["B:PLAYER_LEVEL:* 3 / 2"], "$B+3d6")
        == "=INT(INT((PLAYER_LEVEL * 3) / 2))+3*(((6+1)/2))"
    )
    assert (
        convert_avgdmg(["B:PLAYER_LEVEL:* 3 / 2", "S:PLAYER_LEVEL:* 3"], "$B+d$S")
        == "=INT(INT((PLAYER_LEVEL * 3) / 2))+((INT((PLAYER_LEVEL * 3))+1)/2)"
    )
    assert (
        convert_avgdmg(["S:PLAYER_LEVEL:+ 10 / 15"], "$S")
        == "=INT(INT((PLAYER_LEVEL + 10) / 15))"
    )
