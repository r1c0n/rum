"""Shared guest checks for the paged help, stream tools and colored shell."""
TEXT = b"alpha beta\r\ngamma\nlast"
BINARY = bytes(range(32)) + b"A\0Z\xff"


def seed(images, disk, source):
    for name, data in (("TOOLS.TXT", TEXT), ("BYTES.BIN", BINARY)):
        source.write_bytes(data)
        images.command("mcopy", "-i", str(disk), str(source), "::/" + name)


def run(c, terminal, screen):
    c.command("wc TOOLS.TXT", "2 4 22 TOOLS.TXT\r\n")
    c.command("echo one two | wc -w", "2\r\n")
    c.command("wc -c BYTES.BIN", "36 BYTES.BIN\r\n")
    c.command("head -n 1 TOOLS.TXT > FIRST.TXT")
    c.command("cat TOOLS.TXT | head -n 1 > PIPE.TXT")
    c.command("head -n 1 BYTES.BIN > SLICE.BIN")
    c.command("head -n 0 < BYTES.BIN > ZERO.TXT")
    c.command("more BYTES.BIN > MORE.BIN")
    c.command("hexdump BYTES.BIN > HEX.TXT")
    c.command("help | cat > HELP.TXT")
    c.command("ls / > LIST.TXT")
    c.command("help --all", "hexdump [path]")
    c.command("help missing", "unknown topic")
    c.command("head -n 4294967296 TOOLS.TXT", "Usage: head")

    c.command("clear")
    c.type("echo color")
    raw, _ = terminal.wait_screen(c, screen, lambda rows: rows[0].rstrip() == "/> echo color")
    assert raw[1] == 0x0b and raw[3] == 0x0a and raw[7] == 0x0f, "path, marker and input colors"
    c.send("ret"); c.wait("/> ")

    c.type("help\n")
    raw, first = terminal.wait_screen(c, screen, lambda rows: rows[0].startswith("rum help") and rows[1].startswith("rum commands"))
    assert raw[1] == 0x1f and raw[(23 * 80) * 2 + 1] == 0x1e and "uptime:" in first[24]
    c.send("pgdn")
    terminal.wait_screen(c, screen, lambda rows: any(row.rstrip() == "text:" for row in rows[1:23]))
    c.send("pgup")
    terminal.wait_screen(c, screen, lambda rows: rows[1].startswith("rum commands"))
    c.send("q"); c.wait("/> ")

    c.type("more SCROLL.TXT\n")
    terminal.wait_screen(c, screen, lambda rows: rows[0].startswith("SCROLL.TXT") and rows[1].startswith("oldline000"))
    c.send("pgdn")
    terminal.wait_screen(c, screen, lambda rows: rows[1].startswith("oldline022"))
    c.send("pgup")
    terminal.wait_screen(c, screen, lambda rows: rows[1].startswith("oldline000"))
    c.send("ret")
    terminal.wait_screen(c, screen, lambda rows: rows[1].startswith("oldline001"))
    c.send("q"); c.wait("/> ")
    c.type("more TOOLS.TXT\n")
    _, rows = terminal.wait_screen(c, screen, lambda rows: rows[0].startswith("TOOLS.TXT") and "End |" in rows[23])
    assert rows[1].rstrip() == "alpha beta" and rows[2].rstrip() == "gamma" and rows[3].rstrip() == "last"
    c.send("ctrl", "c"); c.wait("/> ")
    c.command("echo pager restored", "pager restored\r\n")
    c.command("clear"); c.command("ls /")
    raw, rows = terminal.screen(c, screen)
    directory = next(y for y, row in enumerate(rows[:24]) if row.rstrip() == "DOCS/")
    assert raw[directory * 160 + 1] == 0x09, "directory listing color"


def verify(images, disk):
    for name in ("FIRST.TXT", "PIPE.TXT"):
        assert images.file_bytes(disk, "/" + name) == b"alpha beta\r\n"
    assert images.file_bytes(disk, "/SLICE.BIN") == BINARY[:11]
    assert images.file_bytes(disk, "/ZERO.TXT") == b""
    assert images.file_bytes(disk, "/MORE.BIN") == BINARY
    dump = images.file_bytes(disk, "/HEX.TXT")
    assert dump.startswith(b"00000000  00 01 02 03") and b"00000020  41 00 5A FF" in dump and b"|A.Z.|\n" in dump
    help_text = images.file_bytes(disk, "/HELP.TXT")
    assert b"hexdump [path]" in help_text and b"Space/PgDn" not in help_text and b"\x1b" not in help_text
    listing = images.file_bytes(disk, "/LIST.TXT")
    assert b"DOCS/\n" in listing and b"\x1b" not in listing
