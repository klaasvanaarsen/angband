import win32clipboard as clip
import win32con


# ---- HTML clipboard helper ----
# Chrome/GSheets can recognize CF_HTML if it's in the proper "HTML Clipboard Format"
def set_html_clipboard(html: str, plain_text: str = ""):
    """
    Put HTML on the clipboard in the CF_HTML format, along with plain text.
    """
    clip.OpenClipboard()
    clip.EmptyClipboard()

    # Plain text
    clip.SetClipboardData(win32con.CF_UNICODETEXT, plain_text)

    # Insert fragment markers
    if "<body>" in html:
        html = html.replace("<body>", "<body><!--StartFragment-->", 1)
        html = html.replace("</body>", "<!--EndFragment--></body>", 1)
    else:
        html = "<!--StartFragment-->" + html + "<!--EndFragment-->"

    # Build header
    header = (
        "Version:1.0\r\n"
        "StartHTML:{:08d}\r\n"
        "EndHTML:{:08d}\r\n"
        "StartFragment:{:08d}\r\n"
        "EndFragment:{:08d}\r\n"
    )
    start_html = len(header.format(0, 0, 0, 0).encode("utf-8"))
    start_fragment = html.find("<!--StartFragment-->") + start_html
    end_fragment = html.find("<!--EndFragment-->") + start_html
    end_html = start_html + len(html.encode("utf-8"))
    cf_html = header.format(start_html, end_html, start_fragment, end_fragment) + html

    cf_html_format = clip.RegisterClipboardFormat("HTML Format")
    clip.SetClipboardData(cf_html_format, cf_html)

    clip.CloseClipboard()


# ---- List all clipboard formats ----
def list_clipboard_formats():
    clip.OpenClipboard()
    formats = []
    fmt = clip.EnumClipboardFormats(0)
    while fmt:
        try:
            name = clip.GetClipboardFormatName(fmt)
        except:
            name = "<system format>"
        formats.append((fmt, name))
        fmt = clip.EnumClipboardFormats(fmt)
    clip.CloseClipboard()
    return formats


# ---- Example usage ----
html_data = """
<html>
<body>
<table>
<tr><td>=1+2</td><td>=SUM(1,2,3)</td></tr>
<tr><td style="color:red">Red text</td><td style="font-weight:bold">font-weight:bold</td><td>bold tags</td></tr>
</table>
</body>
</html>
"""

plain_text = "=1+2\t=SUM(1,2,3)\tRed text plain\tBold plain"

set_html_clipboard(html_data, plain_text)

formats = list_clipboard_formats()
print("Clipboard contains these formats:")
for f, name in formats:
    print(f"  {f}: {name}")
