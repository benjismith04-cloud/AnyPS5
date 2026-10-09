import json
from pathlib import Path
import struct
import subprocess
import sys
import tempfile

from test_guest_intel_trampolines import main_fixture, pe_sections
from test_guest_symbol_names import dynamic_tags, file_offset, guest_module


RELATIVE = (0x22E0, 8, 0x1000)
GLOB_DAT = (0x22E8, (2 << 32) | 6, 0)
JUMP_SLOT = (0x22F0, (2 << 32) | 7, 0)
SENTINEL = 0x123456789ABCDEF0


def fixture(none=None, lifecycle=None):
    image = guest_module([("local", True), ("external", False)])
    rela = [RELATIVE, GLOB_DAT]
    plt = [JUMP_SLOT]
    if none is not None:
        rela = [none, RELATIVE, none, GLOB_DAT]
        plt = [none, JUMP_SLOT, none]
    struct.pack_into("<Q", image, 0x668, len(rela) * 24)
    for index, entry in enumerate(rela):
        struct.pack_into("<QQq", image, 0x900 + index * 24, *entry)
    for index, entry in enumerate(plt):
        struct.pack_into("<QQq", image, 0x960 + index * 24, *entry)
    tags = [(23, 0x2360), (2, len(plt) * 24), (20, 7), (3, 0x22C8)]
    if lifecycle is not None:
        image.extend(struct.pack("<Q", lifecycle))
        struct.pack_into("<H", image, 56, 4)
        struct.pack_into("<IIQQQQQQ", image, 232, 1, 4, 0xA00, 0, 0, 8, 8, 8)
        tags += [(25, 0), (27, 8)]
    tags.append((0, 0))
    for index, tag in enumerate(tags, 8):
        struct.pack_into("<qQ", image, 0x600 + index * 16, *tag)
    struct.pack_into("<QQ", image, 208, (8 + len(tags)) * 16, (8 + len(tags)) * 16)
    struct.pack_into("<Q", image, 0x8F8, SENTINEL)
    return image


def relocations(image, tags, address_tag, size_tag):
    start = file_offset(image, tags[address_tag])
    return [struct.unpack_from("<QQq", image, offset)
            for offset in range(start, start + tags[size_tag], 24)]


def check_windows(image):
    sections = pe_sections(image)
    code = next(section for section in sections if section[0] == b".elf0")
    data = next(section for section in sections if section[0] == b".elf1")
    header, = struct.unpack_from("<I", image, 0x3C)
    base, = struct.unpack_from("<Q", image, header + 24 + 24)
    pointer, glob, jump, sentinel = struct.unpack_from("<QQQQ", image, data[3] + 0x2E0)
    assert pointer == base + code[1], (pointer, base, code)
    assert (glob, jump, sentinel) == (0, 0, SENTINEL), (glob, jump, sentinel)


def main():
    relinker = Path(sys.argv[1]).resolve()
    failures = []
    cases = [("baseline", None), ("zero", (0, 0, 0)),
             ("overlap", (GLOB_DAT[0], 2 << 32, 123)),
             ("ignored-fields", (0xFFFFFFFFFFFFFFFF, 0xFFFFFFFF00000000, -1)),
             ("untouched", (0x22F8, 0, 0))]
    with tempfile.TemporaryDirectory(prefix="anyps5-guest-none-") as directory:
        work = Path(directory)

        def convert(name, windows, image, error=None):
            case = work / f"{name}-{windows}"
            (case / "sce_module").mkdir(parents=True)
            source = case / "input.elf"
            source.write_bytes(main_fixture())
            (case / "sce_module" / "module.prx").write_bytes(image)
            output = case / "output.elf"
            result = subprocess.run([str(relinker), "--registry", *(["--windows"] if windows else []),
                                     str(source), str(output)], capture_output=True, text=True, timeout=20)
            if error is not None:
                if result.returncode != 2 or error not in result.stderr:
                    failures.append((name, windows, result.stderr))
                return None
            if result.returncode != 0:
                failures.append((name, windows, result.stderr))
                return None
            registry = json.loads((case / "output.module.prx.guest.prx.registry.json").read_text(encoding="utf-8"))
            assert registry == [{"nid": "external", "library": "", "targetOffset": f"0x{target:X}"}
                                for target in (GLOB_DAT[0], JUMP_SLOT[0])], registry
            return (case / "app0" / "sce_module" / "module.prx.guest.prx").read_bytes()

        for windows in (False, True):
            for name, none in cases:
                image = convert(name, windows, fixture(none))
                if image is None:
                    continue
                if windows:
                    check_windows(image)
                else:
                    tags = dynamic_tags(image)
                    assert tags[30] & 8, tags
                    expected_rela = [RELATIVE, GLOB_DAT]
                    expected_plt = [JUMP_SLOT]
                    if none is not None:
                        expected_rela = [(0, 0, 0), RELATIVE, (0, 0, 0), GLOB_DAT]
                        expected_plt = [(0, 0, 0), JUMP_SLOT, (0, 0, 0)]
                    assert relocations(image, tags, 7, 8) == expected_rela
                    assert relocations(image, tags, 23, 2) == expected_plt
            convert("empty-lifecycle", windows, fixture((0, 0, 0), lifecycle=0))
            convert("unrelocated-lifecycle", windows, fixture((0, 0, 0), lifecycle=0x1000),
                    "Unrelocated guest lifecycle pointer")
    if failures:
        raise AssertionError(failures)
    print("Guest no-op relocation tests passed")


if __name__ == "__main__":
    main()
