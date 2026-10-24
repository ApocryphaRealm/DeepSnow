#!/usr/bin/env python3
"""
build-esp.py - writes Deep Snow's plugin, dist/DeepSnow.esp (an ESPFE: a .esp carrying the light flag).

Run from anywhere:  py -3 tools/build-esp.py [out.esp]      (default: <repo>/dist/DeepSnow.esp)

Why a plugin at all (PLAN.md, "Mechanism"): the owner asked for movement changes as SPELL EFFECTS that stack with
Inertia, Dynamic Sprint and Survival Mode. An ability added to an actor with AddSpell is written into the save. A form
created at run time has no stable id across loads, so a saved reference to it would point at nothing; a form from a
plugin keeps its id, and removing the plugin removes the effect cleanly. So the forms ship here, and the DLL edits
only their magnitudes at run time (never saved: plugin forms are not change-tracked for that).

Records (master Skyrim.esm only for convention; nothing of it is referenced). Local ids are what the DLL looks up:
    0x800  KYWD  DeepSnow_EffectKeyword   on both effects, for other mods' conditions
    0x801  MGEF  DeepSnow_SlowEffect      Value Modifier, SpeedMult, detrimental, recover, constant, self
    0x802  MGEF  DeepSnow_FastEffect      Value Modifier, SpeedMult, beneficial, recover, constant, self
    0x803-0x807  SPEL  DeepSnow_Band1..5  abilities, one effect 0x801, magnitude 5/12/20/28/35 (100 - band speed %)
    0x808  SPEL  DeepSnow_RoadBonus       ability, one effect 0x802, magnitude 10

Everything is written from scratch with the field layouts xEdit documents for Skyrim SE (form version 44). The
magnitudes here are the defaults only; the DLL writes the INI's values over them at data load.
"""
import os
import struct
import sys

FORM_VER = 44
MASTER_INDEX = 0x01  # one master (Skyrim.esm) -> our own forms are 0x01xxxxxx in the file
KYWD = 0x800
MGEF_SLOW = 0x801
MGEF_FAST = 0x802
BAND_SPELLS = [0x803, 0x804, 0x805, 0x806, 0x807]
ROAD_SPELL = 0x808
BAND_MAGNITUDES = [5.0, 12.0, 20.0, 28.0, 35.0]  # 100 - (95, 88, 80, 72, 65)
ROAD_MAGNITUDE = 10.0                            # 110 % - 100

AV_SPEEDMULT = 30
NONE = 0xFFFFFFFF

# MGEF flags (xEdit: Magic Effect DATA flags)
F_RECOVER = 0x00000002
F_DETRIMENTAL = 0x00000004
F_NO_DURATION = 0x00000200
F_NO_AREA = 0x00000800
F_PAINLESS = 0x04000000

# SPIT flags
S_MANUAL_COST = 0x00000001
S_IGNORE_RESISTANCE = 0x00100000
S_NO_ABSORB_REFLECT = 0x00200000
SPELL_TYPE_ABILITY = 4


def fid(local):
    return (MASTER_INDEX << 24) | local


def z(text):
    return text.encode('utf-8') + b'\x00'


def sub(sig, data):
    assert len(data) < 0x10000, sig
    return sig.encode('ascii') + struct.pack('<H', len(data)) + data


def rec(sig, formid, body, flags=0):
    return (sig.encode('ascii') + struct.pack('<III', len(body), flags, formid)
            + struct.pack('<HHHH', 0, 0, FORM_VER, 0) + body)


def grup(label, payload):
    return b'GRUP' + struct.pack('<I', len(payload) + 24) + label.encode('ascii') + struct.pack('<i', 0) + b'\x00' * 8 + payload


def mgef_data(flags):
    d = b''
    d += struct.pack('<I', flags)          # flags
    d += struct.pack('<f', 0.0)            # base cost
    d += struct.pack('<I', 0)              # associated item
    d += struct.pack('<I', NONE)           # magic skill: none
    d += struct.pack('<I', NONE)           # resist value: none
    d += struct.pack('<HH', 0, 0)          # counter effect count, unused
    d += struct.pack('<I', 0)              # casting light
    d += struct.pack('<f', 0.0)            # taper weight
    d += struct.pack('<I', 0)              # hit shader
    d += struct.pack('<I', 0)              # enchant shader
    d += struct.pack('<I', 0)              # minimum skill level
    d += struct.pack('<I', 0)              # spellmaking area
    d += struct.pack('<f', 0.0)            # spellmaking casting time
    d += struct.pack('<f', 0.0)            # taper curve
    d += struct.pack('<f', 0.0)            # taper duration
    d += struct.pack('<f', 0.0)            # second AV weight
    d += struct.pack('<I', 0)              # archetype 0 = Value Modifier
    d += struct.pack('<I', AV_SPEEDMULT)   # actor value
    d += struct.pack('<I', 0)              # projectile
    d += struct.pack('<I', 0)              # explosion
    d += struct.pack('<I', 0)              # casting type 0 = constant effect
    d += struct.pack('<I', 0)              # delivery 0 = self
    d += struct.pack('<I', NONE)           # second actor value: none
    d += struct.pack('<I', 0)              # casting art
    d += struct.pack('<I', 0)              # hit effect art
    d += struct.pack('<I', 0)              # impact data set
    d += struct.pack('<f', 0.0)            # skill usage multiplier
    d += struct.pack('<I', 0)              # dual casting
    d += struct.pack('<f', 1.0)            # dual casting scale
    d += struct.pack('<I', 0)              # enchant art
    d += struct.pack('<I', 0)              # hit visuals
    d += struct.pack('<I', 0)              # enchant visuals
    d += struct.pack('<I', 0)              # equip ability
    d += struct.pack('<I', 0)              # image space modifier
    d += struct.pack('<I', 0)              # perk to apply
    d += struct.pack('<I', 2)              # casting sound level 2 = silent
    d += struct.pack('<f', 0.0)            # script effect AI score
    d += struct.pack('<f', 0.0)            # script effect AI delay time
    assert len(d) == 152, len(d)
    return d


def mgef(local, edid, name, desc, flags):
    body = (sub('EDID', z(edid)) + sub('FULL', z(name))
            + sub('KSIZ', struct.pack('<I', 1)) + sub('KWDA', struct.pack('<I', fid(KYWD)))
            + sub('DATA', mgef_data(flags)) + sub('DNAM', z(desc)))
    return rec('MGEF', fid(local), body)


def spel(local, edid, name, desc, effect, magnitude):
    spit = struct.pack('<IIIfIIffI', 0, S_MANUAL_COST | S_IGNORE_RESISTANCE | S_NO_ABSORB_REFLECT, SPELL_TYPE_ABILITY,
                       0.0, 0, 0, 0.0, 0.0, 0)
    assert len(spit) == 36
    body = (sub('EDID', z(edid)) + sub('OBND', b'\x00' * 12) + sub('FULL', z(name)) + sub('DESC', z(desc))
            + sub('SPIT', spit) + sub('EFID', struct.pack('<I', fid(effect)))
            + sub('EFIT', struct.pack('<fII', magnitude, 0, 0)))
    return rec('SPEL', fid(local), body)


def build():
    kywd = rec('KYWD', fid(KYWD), sub('EDID', z('DeepSnow_EffectKeyword')))
    slow = mgef(MGEF_SLOW, 'DeepSnow_SlowEffect', 'Deep Snow', 'Wading through deep snow slows you down.',
                F_RECOVER | F_DETRIMENTAL | F_NO_DURATION | F_NO_AREA | F_PAINLESS)
    fast = mgef(MGEF_FAST, 'DeepSnow_FastEffect', 'Road', 'The road is easy going.',
                F_RECOVER | F_NO_DURATION | F_NO_AREA)
    spells = [spel(BAND_SPELLS[i], 'DeepSnow_Band%d' % (i + 1), 'Deep Snow', 'Deep snow slows you down.', MGEF_SLOW,
                   BAND_MAGNITUDES[i]) for i in range(5)]
    spells.append(spel(ROAD_SPELL, 'DeepSnow_RoadBonus', 'Road', 'The road is easy going.', MGEF_FAST, ROAD_MAGNITUDE))

    groups = [grup('KYWD', kywd), grup('MGEF', slow + fast), grup('SPEL', b''.join(spells))]
    n_records = 1 + 2 + len(spells) + len(groups)  # records and groups, as xEdit counts them
    hedr = struct.pack('<fiI', 1.71, n_records, 0x809)
    header_body = (sub('HEDR', hedr) + sub('CNAM', z('ApocryphaRealm'))
                   + sub('SNAM', z('Deep Snow - depth-band speed abilities (SpeedMult modifiers) for the Deep Snow SKSE plugin.'))
                   + sub('MAST', z('Skyrim.esm')) + sub('DATA', struct.pack('<Q', 0)))
    header = rec('TES4', 0, header_body, flags=0x200)  # 0x200 = light (ESPFE)
    return header + b''.join(groups)


def main():
    repo = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
    out = sys.argv[1] if len(sys.argv) > 1 else os.path.join(repo, 'dist', 'DeepSnow.esp')
    data = build()
    with open(out, 'wb') as f:
        f.write(data)
    print('wrote %s (%d bytes)' % (out, len(data)))


if __name__ == '__main__':
    main()
