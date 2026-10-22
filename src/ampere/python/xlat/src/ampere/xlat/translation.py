# Copyright (c) 2018-2024 Ampere Computing. All rights reserved.
# SPDX-License-Identifier: BSD-3-Clause

import inspect
import itertools
import traceback
import types
from typing import List, Dict
from . import fields, XlatContext, XlatError, uop

all_translations = []
all_fusions = []

enforce_fusion_requirements = False


class TranslationError(RuntimeError):
    pass


class TranslationType(type):
    def __init__(cls, name, bases, attrs, **kwargs):
        """Initialize a new sublcass of Translation.

        This magical method is used to keep track of all the Translations that have been defined
        so we can iterate over them later.
        """
        caller = traceback.extract_stack(limit=2)[0]
        cls.filename = caller.filename
        cls.lineno = caller.lineno
        super().__init__(name, bases, attrs)
        TranslationType.prepare(cls)
        if 'decode' in attrs:
            all_translations.append(cls)

    @staticmethod
    def prepare(cls):
        TranslationType.prepare_patterns(cls)
        TranslationType.prepare_fields(cls)

    @staticmethod
    def prepare_patterns(cls):
        if cls.pattern:
            cls.patterns = fields.PatternParser(cls.pattern).patterns()

    @staticmethod
    def prepare_fields(cls):
        """Parse pfields if it was set, and merge the result with fields to make _unified_fields."""
        if cls.pfields:
            cls._unified_fields = cls.fields.copy()
            cls._unified_fields.update(fields.parse_fields(cls.pattern, cls.pfields))
        else:
            cls._unified_fields = cls.fields


class Translation(metaclass=TranslationType):
    pattern_signal = 'Instruction0'
    pattern = None
    pfields = None
    fields = {}

    def __init__(self, pattern, pattern_signal=None):
        self.pattern = pattern
        if pattern_signal is None:
            pattern_signal = self.pattern_signal
        self.fields = types.SimpleNamespace(**dict([(name, fields.Field(name, pattern_signal, pattern, spec))
                                                    for name, spec in self._unified_fields.items()]))

    def faults(self, f):
        return {}


class FusionType(type):
    def __init__(cls, name, bases, attrs, **kwargs):
        super().__init__(name, bases, attrs)
        caller = traceback.extract_stack(limit=2)[0]
        cls.filename = caller.filename
        cls.lineno = caller.lineno
        if 'decode' in attrs:
            all_fusions.append(cls)


class Fusion(metaclass=FusionType):
    def __init__(self, patterns, translations):
        self.patterns = patterns
        self.translations = translations


def gen_translation_groups() -> List[List[Translation]]:
    translation_groups = []

    for transcls in all_translations:
        with XlatContext(f'translating {transcls.__name__} from {transcls.filename}:{transcls.lineno}'):
            groups = _group_by_equivalent_uops(do_decode(transcls))
            translation_groups.extend(groups.values())

    for fusioncls in all_fusions:
        with XlatContext(f'translating fusion {fusioncls.__name__} from {fusioncls.filename}:{fusioncls.lineno}'):
            groups = _group_by_equivalent_uops(do_fusion_decode(fusioncls))
            translation_groups.extend(groups.values())

    return sorted(translation_groups, key=lambda x: (x[0].uops[0].filename, x[0].uops[0].lineno))


def _group_by_equivalent_uops(decodings) -> Dict[Translation, List[Translation]]:
    decodings = list(decodings)

    if len(decodings) == 1:
        return {decodings[0]: decodings}

    groups = {}
    groups_list = {}

    while decodings:
        seed = decodings.pop(0)
        groups[seed] = {seed}
        groups_list[seed] = [seed]
        remaining = []

        while decodings:
            other = decodings.pop(0)

            if same_uops(seed, other):
                if other not in groups[seed]:
                    groups[seed].add(other)
                    groups_list[seed].append(other)
            else:
                remaining.append(other)

        decodings = remaining

    return groups_list


def same_uops(m, n):
    if hasattr(m, 'mode_bits'):
        if hasattr(n, 'mode_bits'):
            if m.mode_bits != n.mode_bits:
                return False
        else:
            return False

    return len(m.uops) == len(n.uops) and all([same_uop(x, y) for x, y in zip(m.uops, n.uops)])


def same_uop(x, y):
    if x.__class__ != y.__class__:
        return False

    x_attrs = sorted([a for a in dir(x) if not a.startswith('_')])
    y_attrs = sorted([a for a in dir(y) if not a.startswith('_')])

    if x_attrs == y_attrs:
        for a in x_attrs:
            if a in {'translation', 'filename', 'lineno'}:
                continue
            xa = getattr(x, a)
            if inspect.ismethod(xa):
                continue
            if getattr(x, a) != getattr(y, a):
                return False
        return True

    return False


def do_decode(transcls: Translation):
    """Starting with patterns with dont-cares, we progressivly
       expand/concretize the patterns to generate real bits. The
       fields.py value() (which relies on _extract_bits()) method
       throws the exception when dontcares are encountered. We
       repeatdly call decode() until the relevant dontcare values are
       enumerated and the associated uops are extracted.
    """
    modes = getattr(transcls, 'mode_bits', {})

    for mode in expand_modes(modes):
        patterns = transcls.patterns.copy()

        while patterns:
            try:
                pattern = patterns.pop(0)
                trans = transcls(pattern)
                trans.mode_bits = mode
                uop.Uop.uop_sequence.clear()
                trans.fault_conditions = trans.faults(trans.fields)
                result = trans.decode(trans.fields)  # call the decode() function in xlat
                if result is not NotImplemented and uop.Uop.uop_sequence:
                    trans.uops = uop.Uop.uop_sequence.copy()
                    for u in trans.uops:
                        u.translation = trans
                    yield trans
            except fields.NeedDefiniteValue as e:
                # concretize the dontcare values and we'll try again
                patterns.extend(expand_pattern(e.pattern, e.hi, e.lo))


class MultiPatternCombo:
    """Provide an iterator through a multi-dimensional array of patterns.

    A pattern table looks something like this:

        [ [ xxxx,   [ mmmm ],   [ aaaa,  ]
            yyyy,                 bbbb ]
            zzzz ],

    That is, each column has some number of patterns which may be different than the
    others. "index" is actually a vector of indices noting which combination (across)
    of patterns we're currently on. For example:

        index of 0, 0, 0 ==> [ xxxx, mmmm, aaaa ]
        index of 2, 0, 1 ==> [ zzzz, mmmm, bbbb ]

    The first complexity in iterating is just incrementing index, kind of like a
    funny odometer.

    The second complexity is that the contents of the pattern table may actually
    *change* while we're iterating. That happens when one of the patterns we're
    considering has to be "expanded" by turning some don't-care bits into ones or
    zeros. For example we may discover that yyyy has to be broken into two patterns,
    vvvv and wwww:

        [ [ xxxx,   [ mmmm ],   [ aaaa,  ]
            vvvv,                 bbbb ]
            wwww,
            zzzz ],

    By doing this operation "in place" with a slice, we can just keep on iterating
    by backing up to the previous index.
    """
    def __init__(self, patterns, classes):
        self.patterns = patterns
        self.classes = classes
        self.index = [0] * len(self.patterns)
        self._done = False

    def __iter__(self):
        return self

    def __next__(self):
        if self._done:
            raise StopIteration
        pattern_combo = tuple([self.patterns[i][j] for i, j in enumerate(self.index)])
        class_combo = tuple([self.classes[i][j] for i, j in enumerate(self.index)])
        self._previous_index = self.index.copy()
        self._increment_index()
        return pattern_combo, class_combo

    def _increment_index(self):
        for j in range(len(self.index)):
            if j == len(self.index) - 1 and self.index[j] == len(self.patterns[j]) - 1:
                self._done = True

            if self.index[j] < len(self.patterns[j]) - 1:
                self.index[j] += 1
                break
            else:
                self.index[j] = 0

    def expand(self, index, new_patterns):
        expanding_index = self._previous_index[index]
        self.patterns[index][expanding_index:expanding_index + 1] = new_patterns
        self.classes[index][expanding_index:expanding_index + 1] = \
            [self.classes[index][expanding_index]] * len(new_patterns)
        self.index = self._previous_index
        self._done = False


def do_fusion_decode(fusioncls):
    patterns, classes = collect_fusion_patterns(fusioncls.sequence)
    combinations = MultiPatternCombo(patterns, classes)

    for pattern_set, classes in combinations:
        try:
            trans_set = [classes[i](pattern_set[i], f'Instruction{i}') for i in range(len(pattern_set))]
            fusion = fusioncls(pattern_set, trans_set)
            uop.Uop.uop_sequence.clear()

            all_requirements_met = True

            if enforce_fusion_requirements and hasattr(fusion, 'requirements'):
                for i, trans in enumerate(trans_set):
                    requirement_function = fusion.requirements[i]
                    if requirement_function is not None:
                        if not requirement_function(trans.fields):
                            all_requirements_met = False
                            break

            if all_requirements_met:
                result = fusion.decode(*[trans.fields for trans in trans_set])
                if result is not NotImplemented and uop.Uop.uop_sequence:
                    fusion.uops = uop.Uop.uop_sequence.copy()
                    for u in fusion.uops:
                        u.translation = fusion
                    yield fusion
        except fields.NeedDefiniteValue as e:
            index = pattern_set.index(e.pattern)
            combinations.expand(index, expand_pattern(e.pattern, e.hi, e.lo))


def collect_fusion_patterns(sequence):
    patterns = []
    classes = []

    for trans in sequence:
        trans_patterns = []
        trans_classes = []

        if type(trans) is TranslationType:
            trans_patterns = trans.patterns.copy()
            trans_classes = [trans] * len(trans_patterns)
        else:
            if callable(trans):
                # A callable will be treated as a predicate function to select a set of matching Translations.
                # FIXME: not efficient; should probably do this in an early pass
                #
                trans_list = [t for t in all_translations if trans(t)]
            else:
                if isinstance(trans, set):
                    raise XlatError(
                        'illegal Python "set" in Fusion sequence; try using a deterministic container like a "list"')
                trans_list = trans

            for t in trans_list:
                new_patterns = t.patterns.copy()
                trans_patterns.extend(new_patterns)
                trans_classes.extend([t] * len(new_patterns))

        patterns.append(trans_patterns)
        classes.append(trans_classes)

    return patterns, classes


def expand_pattern(pattern, hi, lo):
    hi = 31 - hi
    lo = 31 - lo + 1
    prefix, bits, suffix = pattern[0:hi], pattern[hi:lo], pattern[lo:]
    return [''.join(prefix + x + suffix) for x in expand_bits(list(bits))]


def expand_bits(indefinite_bits, definite_bits=[]):
    if not indefinite_bits:
        yield ''.join(definite_bits)
    else:
        if indefinite_bits[0] != '1':
            yield from expand_bits(indefinite_bits[1:], definite_bits + ['0'])
        if indefinite_bits[0] != '0':
            yield from expand_bits(indefinite_bits[1:], definite_bits + ['1'])


def expand_modes(mode_bits):
    options = {}
    for name, width in mode_bits.items():
        bits = ['-'] * width
        options[name] = [int(x, 2) for x in expand_bits(bits)]
    for combo in itertools.product(*options.values()):
        yield dict(zip(options.keys(), combo))


class TranslationGenerator:
    def __init__(self, translation_groups=None):
        self._translation_groups = translation_groups

    class DecodedUopIter:
        def __init__(self, translation_generator):
            self._translation_generator = translation_generator

        def __iter__(self):
            self._translation_generator._gen_translations()
            for trans_group in self._translation_generator._translation_groups:
                for trans in sorted(trans_group, key=lambda x: (x.uops[0].filename, x.uops[0].lineno)):
                    for index, u in enumerate(trans.uops):
                        yield ((trans, u, index), True)

    class TranslationIter:
        def __init__(self, translation_generator):
            self._translation_generator = translation_generator

        def __iter__(self):
            self._translation_generator._gen_translations()
            for trans_group in self._translation_generator._translation_groups:
                for trans in sorted(trans_group, key=lambda x: (x.uops[0].filename, x.uops[0].lineno)):
                    yield (trans, True)

    class TranslationGroupIter:
        def __init__(self, translation_generator):
            self._translation_generator = translation_generator

        def __iter__(self):
            self._translation_generator._gen_translations()
            for trans_group in self._translation_generator._translation_groups:
                yield (trans_group, True)

    def each_translation_group(self):
        return self.TranslationGroupIter(self)

    def each_translation(self):
        return self.TranslationIter(self)

    def each_decoded_uop(self):
        return self.DecodedUopIter(self)

    def _gen_translations(self):
        if self._translation_groups is None:
            self._translation_groups = gen_translation_groups()


_translation_generator = TranslationGenerator()

translation_groups = _translation_generator.each_translation_group()
translations = _translation_generator.each_translation()
decoded_uops = _translation_generator.each_decoded_uop()
