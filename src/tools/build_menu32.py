# UI bundles of the Single Race mod for NFS Hot Pursuit (2010).
#   python build_menu32.py main_menu <UI\SCREENS\MAIN_MENU.BNDL> <single_race_menu.luac> <out MAIN_MENU.BNDL>
#   python build_menu32.py wall <UI\SCREENS\EVENT_SELECT.BNDL> <UI\SCREENS\EVENT_MAP.BNDL> <out WALL.BNDL>
# main_menu: adds single_race_menu.lua (32-bit Lua 5.1 bytecode) and lists it after main_menu in MAIN_MENU.JSON.
# wall:      the multiplayer event picker as the "wall" page, for offline use (as Remastered wall_sr_v2):
#   - the location's full event list (Location.Events, as the career map) instead of the lobby's mode-keyed list;
#   - the career map's gamercard / user_feedback / report_content scripts, controls and adapters;
#   - event_select.json renamed to wall.json.
import sys, struct, binascii, json, re
from bnd32 import Bundle

TEXTFILE = 0x70


def crc(name):
    return binascii.crc32(name.lower().encode())


def payload(body):
    p = struct.pack('<I', len(body)) + body
    return p + bytes((-len(p)) % 16)


def text_of(b, name):
    raw = b.data(b.find(crc(name)))
    n, = struct.unpack_from('<I', raw, 0)
    return raw[4:4 + n].decode('latin1')


def set_text(b, name, text):
    b.set_data(b.find(crc(name)), payload(text.encode('latin1')))


def add_textfile(b, name, body):
    assert not b.has(crc(name)), name
    tmpl = next(x for x in b.ents if b.type(x) == TEXTFILE)
    e = bytearray(tmpl['e'])
    struct.pack_into('<Q', e, 0, crc(name))
    return b.add(e, [payload(body), None, None, None])


def copy_resource(dst, src, rid):
    assert not dst.has(rid)
    x = src.find(rid)
    y = dst.add(x['e'], [None] * 4)
    y['e'][:] = x['e']
    y['comp'] = list(x['comp'])
    y['changed'] = [False] * 4
    return y


def build_main_menu(src, luac, out):
    b = Bundle(open(src, 'rb').read())
    add_textfile(b, 'single_race_menu.lua', open(luac, 'rb').read())
    t = text_of(b, 'main_menu.json')
    m = re.search(r'"main_menu"\s*:\s*"main_menu"\s*,', t)
    assert m and len(re.findall(r'"main_menu"\s*:\s*"main_menu"\s*,', t)) == 1
    t = t[:m.end()] + '\r\n        "single_race_menu": "single_race_menu",' + t[m.end():]
    set_text(b, 'main_menu.json', t)
    open(out, 'wb').write(b.build())


def build_wall(src, map_src, out):
    b = Bundle(open(src, 'rb').read())
    m = Bundle(open(map_src, 'rb').read())
    for name in ('gamercard', 'user_feedback', 'report_content'):
        if not b.has(crc(name + '.lua')):
            copy_resource(b, m, crc(name + '.lua'))
    mt = text_of(m, 'event_map.json')
    mj = json.loads(mt[:mt.rfind('}') + 1])
    t = text_of(b, 'event_select.json')

    # 1) every event of the location, not the lobby's mode-keyed list (offline the lobby mode does not exist)
    pat = (r'("Events"\s*:\s*\{\s*"dataSource"\s*:\s*)"Autolog\.Adapter\.EventsByGameMode\.Events"(\s*,\s*"dataSourceLength"\s*:\s*)'
           r'"Autolog\.Adapter\.EventsByGameMode\.NumEvents"')
    t, n = re.subn(pat, r'\1"Autolog.Adapter.Location.Events"\2"Autolog.Adapter.Location.NumEvents"', t)
    assert n == 1
    t, n = re.subn(r'"EventsByGameMode"\s*:\s*\{[^{}]*LobbyState\.GameModeId[^{}]*\}\s*,\s*', '', t)
    assert n == 1 and 'EventsByGameMode' not in t

    # 2) adapters of the gamercard / report overlay, from the career map
    keys = [k for k in mj['adapters'] if k.startswith(('Gamercard', 'ReportContent'))]
    for k in keys:
        assert '"%s"' % k not in t, k
    ad = ''.join('\r\n        "%s": %s,' % (k, json.dumps(mj['adapters'][k])) for k in keys)
    ma = re.search(r'"adapters"\s*:\s*\{', t)
    t = t[:ma.end()] + ad + t[ma.end():]

    # 3) the gamercard deferred view model
    md = re.search(r'"deferredViewmodels"\s*:\s*\{', t)
    assert md
    empty = t[md.end():].lstrip().startswith('}')
    t = t[:md.end()] + '\r\n        "Gamercard": %s%s' % (json.dumps(mj['deferredViewmodels']['Gamercard']),
                                                          '' if empty else ',') + t[md.end():]

    # 4) scripts and controls
    ms = re.search(r'"event_select"\s*:\s*"event_select"\s*,', t)
    assert ms
    t = t[:ms.end()] + ('\r\n        "gamercard": "gamercard",\r\n        "user_feedback": "user_feedback",'
                        '\r\n        "report_content": "report_content",') + t[ms.end():]
    mc = re.search(r'"controls"\s*:\s*\{', t)
    ctl = ''.join('\r\n        "%s": %s,' % (c, json.dumps(mj['controls'][c])) for c in ('Gamercard', 'UserFeedback', 'ReportContent'))
    t = t[:mc.end()] + ctl + t[mc.end():]
    json.loads(t[:t.rfind('}') + 1])     # still valid JSON

    set_text(b, 'event_select.json', t)
    x = b.find(crc('event_select.json'))
    assert not b.has(crc('wall.json'))
    struct.pack_into('<Q', x['e'], 0, (struct.unpack_from('<Q', x['e'], 0)[0] & ~0xFFFFFFFF) | crc('wall.json'))
    open(out, 'wb').write(b.build())


if __name__ == '__main__':
    which = sys.argv[1]
    {'main_menu': build_main_menu, 'wall': build_wall}[which](*sys.argv[2:])
    print('ok', which)
