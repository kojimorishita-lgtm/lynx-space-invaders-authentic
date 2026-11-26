#!/usr/bin/env python3
"""Known ROM banks for the build, and content-based detection.

Every piece of arcade data this port extracts lives in the
final 2 KiB bank of the ROM image ($1800-$1FFF).  A bank is
identified purely by the SHA-256 of its 2048 bytes -- file
names and set names play no part.  Only hashes are kept here;
a hash is not game data.

A bank may reach the builder as:
  * one 2 KiB file,
  * the upper or lower window of a larger file (found by
    hashing every 1 KiB-aligned 2 KiB window),
  * two 1 KiB files (first half $1800-$1BFF, second half
    $1C00-$1FFF), assembled via HALVES below.

Banks whose extracted blocks all match the original set are
listed with the standard offsets.  Banks that differ only in
visual data (the font) use the same offsets; the differing
bytes are taken from the bank itself.  The one bank whose
march tables sit at shifted addresses carries an override.
Anything not listed here stops the build with a report.
"""
import hashlib
import os
import sys

# Extraction addresses within the 8 KiB image.
STANDARD_OFFSETS = {
    "font": 0x1E00,
    "ufo_points": 0x1D54,
    "demo_cmds": 0x1F74,
    "col_fire": 0x1D00,
    "march_thr": 0x1A11,
    "march_del": 0x1A21,
    "reload_tab": 0x1CB8,
    "reload_rates": 0x1AA1,
    "squiggly": 0x1CD0,
    "saucer_exp": 0x1D7C,
}

# bank SHA-256 -> {"sets": [...], "offsets": {overrides}}
BANKS = {
    "0a97ed62e2063c13e373a9a61ea3d8fbb701e414af05953e16f823fd879d5f49": {"sets": "invaders(parent)", "offsets": {}},
    "0a9ca889c96ffff05572ad2fec5252be58d4d0fd8b3662fb2b835ba0766728da": {"sets": "cosmicin", "offsets": {}},
    "1f98d93041dcf2d446f6ddb28b5a0987a6170f11ebecab417faf0928a942fe2e": {"sets": "spceking", "offsets": {}},
    "224b7a8f31285f9d382f6f8641b20bb046821a299d91afa51e4a104b3065856a": {"sets": "spaceatt, spaceatt2k", "offsets": {}},
    "27314521122716666c3c656f39c3746a943f1b30490baea97a1f936faa2ce252": {"sets": "invasiona2", "offsets": {}},
    "47cba71f69c836431a7b986f70ac3f725c83fa64c915f5e8c182c737cf9246dd": {"sets": "jspecter2", "offsets": {}},
    "4f0a9703148ac5ae87d2d6cf11e51eac20ba3de5a10169208f47bd4c09f877eb": {"sets": "invaderl", "offsets": {}},
    "514cabc4c5a3c91e8506ad460dc26b12ac1dd4d12aa12b64bf3b2e0eba4ef3b1": {"sets": "invasionrza", "offsets": {}},
    "584c4a709f5899e61eaa171eefe257292abb6c1a426540b691c2ec82a041e50c": {"sets": "sisv", "offsets": {}},
    "599bdd7d38761d52b766b44f0edbfa74ded0eb8627f8887eef7e36daac9f4ade": {"sets": "sinvemag2", "offsets": {}},
    "5e5dfafd50005a69c5722615bd0879f8e59b7b11a783831fa8769e93d51a9be9": {"sets": "sitv", "offsets": {}},
    "81d4ddf697d22fa51b7fe4a7d7adb4af2a2a65c7402ee6b1cfb9c99a983745c1": {"sets": "superinv", "offsets": {}},
    "8ef4f94834698a07e0d8bca7a246cb67339f4cd3cb7cbebd52a71f5a5beff4c6": {"sets": "sinvemag", "offsets": {}},
    "942ca441245a700565d3ad47a44f76211f4c414d24369b85b890295d6dfbaf62": {"sets": "sicv, sicv1", "offsets": {"march_thr": 0x19F3, "march_del": 0x1A03}},
    "b59d7f9ecaa47fdf4fcd77f9fb408c06f085edb28c3adf4be5a72471597fafc7": {"sets": "spcebttl", "offsets": {}},
    "bc2528485cffe95a9efac424cb8e670b2ecebd4337fe684500796cedb765e783": {"sets": "jspecter", "offsets": {}},
    "e0f7d081d6f146aec2ea395b60d85075cdde89f8eb4c1731a0322055986742d5": {"sets": "invasiona", "offsets": {}},
    "e9da52b28cb284db407639e539c094a36d51d91f54d40f71e702ae09d6f11406": {"sets": "spacerng", "offsets": {}},
    "f4c7fb60377c1c5f14d417387b2838870fe65d38a60fed6cffa6cefba94aa02d": {"sets": "invasion", "offsets": {}},
}

# SHA-256 of a 1 KiB half -> (bank SHA-256, half index 0/1)
HALVES = {
    "02bc5ef6aab05636ef07a0f2bd3c999ac1f4ed79d39741c21ec37dec86419851": ("224b7a8f31285f9d382f6f8641b20bb046821a299d91afa51e4a104b3065856a", 1),
    "126b1c7ea09b2c14add36a308ceddf61c4a46f1a18466278f599db1598d7a8a6": ("f4c7fb60377c1c5f14d417387b2838870fe65d38a60fed6cffa6cefba94aa02d", 1),
    "14f0f5bdd3e8e78c4284194333e5cd91f6092344175c212be2e2df472e0b5727": ("e0f7d081d6f146aec2ea395b60d85075cdde89f8eb4c1731a0322055986742d5", 0),
    "34bad758ba44f78151327300fba6febd8909132d453f3caf7d8658a7601bd95c": ("27314521122716666c3c656f39c3746a943f1b30490baea97a1f936faa2ce252", 1),
    "38d6c7d5ef4071ee8b2abb55fb6497725a719ffe148a52d1027bb814e1ea030e": ("f4c7fb60377c1c5f14d417387b2838870fe65d38a60fed6cffa6cefba94aa02d", 0),
    "3f1b82f48acd1648c756358e3a2676e3ad647b44097e0ce7b92e54ba5bb3f118": ("8ef4f94834698a07e0d8bca7a246cb67339f4cd3cb7cbebd52a71f5a5beff4c6", 0),
    "48ad38e28de4b6c635cb06fee5e54dbc64ebceb19e24178bf7b88088a5ad0046": ("514cabc4c5a3c91e8506ad460dc26b12ac1dd4d12aa12b64bf3b2e0eba4ef3b1", 1),
    "71c369caa4e421f2e3f421d161237e148c076efeb4dbe1a14e491787fd34d88a": ("b59d7f9ecaa47fdf4fcd77f9fb408c06f085edb28c3adf4be5a72471597fafc7", 1),
    "73fcd655901976cc6eb25018f82d0b4d732de208cf5f4eb1823dcd48b906510c": ("584c4a709f5899e61eaa171eefe257292abb6c1a426540b691c2ec82a041e50c", 0),
    "86847f060e5019b2258b87cf79ba766618f048ba25cfa48a216026b8d6789a6e": ("224b7a8f31285f9d382f6f8641b20bb046821a299d91afa51e4a104b3065856a", 0),
    "8fbb1e03cf064825455f57db71aee8f71cf298e11e46ddf1200d45194f335e24": ("599bdd7d38761d52b766b44f0edbfa74ded0eb8627f8887eef7e36daac9f4ade", 0),
    "93f8c105211fb7bbf963d77178d98521fe3438f8c670a014cae63de9ff24ac1c": ("81d4ddf697d22fa51b7fe4a7d7adb4af2a2a65c7402ee6b1cfb9c99a983745c1", 0),
    "9b2bf06d37498cf13acce2651901f2f99c8bcddaac078ad339addd9f7e38659f": ("81d4ddf697d22fa51b7fe4a7d7adb4af2a2a65c7402ee6b1cfb9c99a983745c1", 1),
    "b10e48fb60adf54cf5c3c932888fe47720825aeaae888ecac987c1a2fc45c8c7": ("8ef4f94834698a07e0d8bca7a246cb67339f4cd3cb7cbebd52a71f5a5beff4c6", 1),
    "b8aa74e5545a389a98603332cab3e487468f14be4abf4d0cc37b31e4d68e56d9": ("e0f7d081d6f146aec2ea395b60d85075cdde89f8eb4c1731a0322055986742d5", 1),
    "c6970c0a436dcc47b11ad3cdb2110f13c85d0a8b4c5bb4a187f9de4ea8ea25c5": ("942ca441245a700565d3ad47a44f76211f4c414d24369b85b890295d6dfbaf62", 0),
    "e140b185c30ed4c2859333d71b53ee66a4ac85f69d00c867d9b1c7b80b172d23": ("27314521122716666c3c656f39c3746a943f1b30490baea97a1f936faa2ce252", 0),
    "e36e49ecb0afb7e864f5d67378d9f0ea7a64b532de4c1318435e613a99578384": ("4f0a9703148ac5ae87d2d6cf11e51eac20ba3de5a10169208f47bd4c09f877eb", 0),
    "e53da82d519c99d30d1dd0df8c8d627a480131193db681fefa33bd92ba96fe45": ("4f0a9703148ac5ae87d2d6cf11e51eac20ba3de5a10169208f47bd4c09f877eb", 1),
    "ee22a7cafb17edef972fec6a5f885be9188b03c098c3bb35a7b4974e49ae0b01": ("599bdd7d38761d52b766b44f0edbfa74ded0eb8627f8887eef7e36daac9f4ade", 1),
    "f4c550986f8ee6aa663d407731880601e0210c51f1dd3fc5b5b6b9d90cf89189": ("942ca441245a700565d3ad47a44f76211f4c414d24369b85b890295d6dfbaf62", 1),
    "f623406bf2f3e30b8a452781d86a3dcc48f3e9bac59969419661b209ab265696": ("584c4a709f5899e61eaa171eefe257292abb6c1a426540b691c2ec82a041e50c", 1),
    "face14babb680e04297df9ca53a444909157d33a2ce6be8154953dd652a5ec86": ("b59d7f9ecaa47fdf4fcd77f9fb408c06f085edb28c3adf4be5a72471597fafc7", 0),
    "fb37a2c4c457fdc6fdb6948d932dfa24845fa372381dd427f6a5c8d9b073f606": ("514cabc4c5a3c91e8506ad460dc26b12ac1dd4d12aa12b64bf3b2e0eba4ef3b1", 0),
}


def _fail(rom_dir, files, reason):
    print(f"error: {reason}")
    print(f"Files found in {rom_dir}:")
    for name, data in files:
        print(f"  {name}: {len(data)} bytes, "
              f"sha256 {hashlib.sha256(data).hexdigest()}")
    print("The build needs a ROM whose final 2 KiB bank is one of")
    print("the known banks listed in tools/romsets.py.  The bank is")
    print("identified by its SHA-256 alone; file names do not matter.")
    sys.exit(1)


def detect_bank(rom_dir):
    """Identify the ROM bank in rom_dir by content.

    Returns (bank_bytes, bank_sha256, source_description).
    Exits with a report when no known bank, or more than one
    different bank, is present.
    """
    if not os.path.isdir(rom_dir):
        print(f"error: {rom_dir} not found.")
        print("Place the ROM files of the arcade board in rom/.")
        sys.exit(1)
    files = []
    for name in sorted(os.listdir(rom_dir)):
        path = os.path.join(rom_dir, name)
        if os.path.isfile(path):
            files.append((name, open(path, "rb").read()))
    if not files:
        print(f"error: no files in {rom_dir}.")
        print("Place the ROM files of the arcade board in rom/.")
        sys.exit(1)

    found = {}  # bank sha -> (bank bytes, source description)
    # Whole 2 KiB files and 1 KiB-aligned windows of larger files.
    for name, data in files:
        for off in range(0, len(data) - 2047, 1024):
            window = data[off:off + 2048]
            digest = hashlib.sha256(window).hexdigest()
            if digest in BANKS:
                src = name if off == 0 and len(data) == 2048 \
                    else f"{name} (offset {off})"
                found.setdefault(digest, (window, src))
    # 1 KiB halves.
    halves = {}
    for name, data in files:
        if len(data) == 1024:
            entry = HALVES.get(hashlib.sha256(data).hexdigest())
            if entry:
                halves.setdefault(entry[0], {})[entry[1]] = (name, data)
    for bank_sha, parts in halves.items():
        if len(parts) == 2:
            bank = parts[0][1] + parts[1][1]
            if hashlib.sha256(bank).hexdigest() == bank_sha:
                found.setdefault(
                    bank_sha,
                    (bank, f"{parts[0][0]} + {parts[1][0]}"))

    if not found:
        _fail(rom_dir, files,
              "no known ROM bank found in rom/.")
    if len(found) > 1:
        _fail(rom_dir, files,
              "more than one different ROM bank found in rom/.")
    bank_sha, (bank, src) = next(iter(found.items()))
    return bank, bank_sha, src


def open_rom(rom_dir):
    """Return (image, offsets, bank_sha256, source_description).

    The image is a virtual 8 KiB image: zero padding up to $1800
    followed by the identified bank, so extraction addresses work
    exactly as documented in STANDARD_OFFSETS.
    """
    bank, bank_sha, src = detect_bank(rom_dir)
    offsets = dict(STANDARD_OFFSETS)
    offsets.update(BANKS[bank_sha]["offsets"])
    return b"\x00" * 0x1800 + bank, offsets, bank_sha, src
