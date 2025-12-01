import win32clipboard as clip
import win32con

SYSTEM_FORMATS = {
    win32con.CF_TEXT: "CF_TEXT",
    win32con.CF_BITMAP: "CF_BITMAP",
    win32con.CF_METAFILEPICT: "CF_METAFILEPICT",
    win32con.CF_SYLK: "CF_SYLK",
    win32con.CF_DIF: "CF_DIF",
    win32con.CF_TIFF: "CF_TIFF",
    win32con.CF_OEMTEXT: "CF_OEMTEXT",
    win32con.CF_DIB: "CF_DIB",
    win32con.CF_PALETTE: "CF_PALETTE",
    win32con.CF_PENDATA: "CF_PENDATA",
    win32con.CF_RIFF: "CF_RIFF",
    win32con.CF_WAVE: "CF_WAVE",
    win32con.CF_UNICODETEXT: "CF_UNICODETEXT",
    win32con.CF_ENHMETAFILE: "CF_ENHMETAFILE",
    win32con.CF_HDROP: "CF_HDROP",
    win32con.CF_LOCALE: "CF_LOCALE",
    win32con.CF_DIBV5: "CF_DIBV5",
}


def list_clipboard_formats():
    clip.OpenClipboard()
    formats = []
    fmt = clip.EnumClipboardFormats(0)
    while fmt:
        try:
            name = clip.GetClipboardFormatName(fmt)
            if not name:
                name = SYSTEM_FORMATS.get(fmt, f"Unknown system format {fmt}")
        except Exception:
            name = SYSTEM_FORMATS.get(fmt, f"Unknown system format {fmt}")
        formats.append((fmt, name))
        fmt = clip.EnumClipboardFormats(fmt)
    clip.CloseClipboard()
    return formats


def get_clipboard_data(fmt):
    clip.OpenClipboard()
    data = None
    try:
        if fmt in (win32con.CF_TEXT, win32con.CF_OEMTEXT):
            data = clip.GetClipboardData(fmt).decode("ascii", errors="replace")
        elif fmt == win32con.CF_UNICODETEXT:
            data = clip.GetClipboardData(fmt)
        else:
            # For unknown or binary formats, get raw bytes
            try:
                data = clip.GetClipboardData(fmt)
            except Exception:
                data = None
    finally:
        clip.CloseClipboard()
    return data


def show_clipboard_contents():
    print("Clipboard formats:")
    for fmt, name in list_clipboard_formats():
        print(f"  {fmt}: {name}")
        content = get_clipboard_data(fmt)
        if content:
            # Decode bytes if they look like UTF-16LE
            if isinstance(content, bytes):
                try:
                    text = content.decode("utf-16le")
                    print(
                        f"    Content (UTF-16LE text, length {len(content)}, first 4000 chars): {text[:4000]!r}"
                    )
                except UnicodeDecodeError:
                    # fallback
                    print(
                        f"    Content (bytes, length {len(content)}, first 4000 chars): {content[:4000]!r}"
                    )
            else:
                # already str
                print(
                    f"    Content (text, length {len(content)}, first 4000 chars): {content[:4000]!r}"
                )
        else:
            print(f"    Content: <binary or empty>")


if __name__ == "__main__":
    show_clipboard_contents()
