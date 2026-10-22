# Copyright (c) 2019-2022 Ampere Computing. All rights reserved.
# SPDX-License-Identifier: BSD-3-Clause

from ampere.xlat import translation, uop


class add(uop.Uop):
    pass


class xor(uop.Uop):
    pass


class Trans(translation.Translation):
    pattern = '1111 0000 1010   ---- 1111 0000  1010  ----'
    pfields = 'one   two  three four five  six  seven eight'

    def decode(self, f):
        add(f.one, f.two, f.four)


class TransGroup(translation.Translation):
    pattern = '- - 000000000000000000000000000000'
    pfields = 'a b'

    def decode(self, f):
        if f.a.value:
            if f.b.value:
                add()
            else:
                xor()
        else:
            if f.b.value:
                xor()
            else:
                add()


class TransGroup2(translation.Translation):
    pattern = '- - 000000000000000000000000000000'
    pfields = 'a b'

    def decode(self, f):
        if f.a.value:
            if f.b.value:
                add(dst=1)
            else:
                add(dst=2)
        else:
            if f.b.value:
                add(dst=2)
            else:
                add(dst=1)


class TransUopClearingBug(translation.Translation):
    pattern = '- - 000000000000000000000000000000'
    pfields = 'a b'

    def decode(self, f):
        if f.a.value:
            add()
            if f.b.value:
                xor()
            else:
                add()
        else:
            if f.b.value:
                xor()
            else:
                add()


class TransGroupDifferentMode(translation.Translation):
    pattern = '- - 000000000000000000000000000000'
    pfields = 'a b'

    mode_bits = {'SameDifference': 1}

    def decode(self, f):
        if self.mode_bits['SameDifference']:
            add()
        else:
            add()


generated_translation_groups = translation.gen_translation_groups()


def find_translations(cls):
    ts = []
    for group in generated_translation_groups:
        for t in group:
            if isinstance(t, cls):
                ts.append(t)
    return ts


def find_translation_groups(cls):
    ts = []
    for group in generated_translation_groups:
        first = next(iter(group))
        if isinstance(first, cls):
            ts.append(group)
    return ts


def test_one_translation():
    ts = find_translations(Trans)
    assert len(ts) == 1
    t = ts[0]
    assert len(t.uops) == 1
    assert t.uops[0].__class__ == add


def test_translation_group():
    ts = find_translation_groups(TransGroup)
    assert len(ts) == 2


def test_translation_group2():
    ts = find_translation_groups(TransGroup2)
    assert len(ts) == 2


def test_uop_clearing_bug():
    ts = find_translations(TransUopClearingBug)
    for t in ts:
        if t.pattern == '00000000000000000000000000000000':
            assert [type(u) for u in t.uops] == [add]
        elif t.pattern == '01000000000000000000000000000000':
            assert [type(u) for u in t.uops] == [xor]
        elif t.pattern == '10000000000000000000000000000000':
            assert [type(u) for u in t.uops] == [add, add]
        elif t.pattern == '11000000000000000000000000000000':
            assert [type(u) for u in t.uops] == [add, xor]
        else:
            assert False


def test_translation_split_group_by_mode():
    ts = find_translation_groups(TransGroupDifferentMode)
    assert len(ts) == 2
