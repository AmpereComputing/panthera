# Copyright (c) 2018-2024 Ampere Computing. All rights reserved.
# SPDX-License-Identifier: BSD-3-Clause

from functools import reduce
from .pattern import Pattern


class NeedDefiniteValue(RuntimeError):
    def __init__(self, pattern, hi, lo):
        self.pattern = pattern
        self.hi = hi
        self.lo = lo


class Field:
    def __init__(self, name, source, pattern, spec):
        self.name = name
        self.source = source
        self.pattern = pattern
        self.hi, self.lo = spec
        self._value = None

    @property
    def value(self):
        if self._value is None:
            self._value = self._extract_bits(self.hi, self.lo)
        return self._value

    @property
    def raw_value(self):
        return self.pattern[31 - self.hi:31 - self.lo + 1]

    @property
    def width(self):
        return self.hi - self.lo + 1

    @property
    def source_width(self):
        return len(self.pattern)

    def _extract_bits(self, hi, lo):
        try:
            pat = self.pattern[31 - hi:31 - lo + 1]
            if pat[0] == '-':  # If we don't check this case manually, we could simply get a negative number.
                raise ValueError
            return int(pat, 2)
        except ValueError:
            raise NeedDefiniteValue(self.pattern, hi, lo)

    def __str__(self):
        return f'{self.source}[{self.hi}:{self.lo}]'

    def __format__(self, format_spec):
        return str(self)

    def __bool__(self):
        return self.value.__bool__()

    def __eq__(self, other):
        return isinstance(other, Field) and self.source == other.source and self.hi == other.hi and self.lo == other.lo


class FieldSet:
    def __init__(self, source, pattern, **fields):
        for name, spec in fields.items():
            field = Field(name, source, pattern, spec)
            setattr(self, field.name, field)

    def __getitem__(self, item):
        return getattr(self, item)


def parse_fields(patstr, fldstr):
    ps = PatternParser(patstr).fields()
    fs = _split_into_words(fldstr)
    return _locate_fields(ps, fs)


def _split_into_words(s):
    ws = s.split(sep=' ')
    col_lo = 0
    words = []
    for w in ws:
        if not w:
            col_lo += 1
        else:
            col_hi = col_lo + len(w) - 1
            words.append((col_lo, col_hi, w))
            col_lo += len(w) + 1
    return words


def _locate_fields(pattern_words, field_words):
    fields = {}

    pattern_words.reverse()
    field_words.reverse()

    for field_col_lo, field_col_hi, field_name in field_words:
        bit_lo = 0

        match_hi = None
        match_lo = None

        for pat_col_lo, pat_col_hi, pat_bit_width in pattern_words:
            bit_hi = bit_lo + pat_bit_width - 1

            if pat_col_lo <= field_col_lo <= pat_col_hi:
                match_hi = (bit_hi, bit_lo)

            if pat_col_lo <= field_col_hi <= pat_col_hi:
                match_lo = (bit_hi, bit_lo)

            bit_lo += pat_bit_width

        if match_hi is None:
            raise RuntimeError(f'field {field_name} does not start within pattern bits')

        field_bit_hi = match_hi[0]
        field_bit_lo = match_lo[1] if match_lo else match_hi[1]

        fields[field_name] = (field_bit_hi, field_bit_lo)

    return fields


class PatternParser:
    class Token:
        def __init__(self, char, col):
            self.char = char
            self.col = col

        @staticmethod
        def tokenize(pattern):
            toks = []
            col = 0
            for c in list(pattern):
                if c != ' ':
                    toks.append(PatternParser.Token(c, col))
                col += 1
            return toks

    def __init__(self, pattern):
        self.tokens = PatternParser.Token.tokenize(pattern)
        self.current_token = None
        self.expr = self.p_or()

    def fields(self):
        return list(self.expr_to_fields(self.expr))

    def patterns(self):
        return list(self.gen_pattern(self.expr))

    def gen_pattern(self, expr):
        if type(expr) is PatternParser.Bits:
            yield expr.bits
        elif type(expr) is PatternParser.Cat:
            for lp in self.gen_pattern(expr.left):
                for rp in self.gen_pattern(expr.right):
                    yield lp + rp
        elif type(expr) is PatternParser.Or:
            yield from self.gen_pattern(expr.left)
            yield from self.gen_pattern(expr.right)
        elif type(expr) is PatternParser.Group:
            yield from self.gen_pattern(expr.left)
        elif type(expr) is PatternParser.Excl:
            def make_pattern(s):
                p = Pattern(len(s))
                p.add_pattern(s)
                return p

            p_includes = [make_pattern(lp) for lp in self.gen_pattern(expr.left)]
            p_excludes = [make_pattern(rp) for rp in self.gen_pattern(expr.right)]

            res = reduce(Pattern.p_and_not, [reduce(Pattern.p_or, p_includes)] + p_excludes)

            for bits in res.p_format():
                yield bits

    class Node:
        def __init__(self, left, right):
            self.left = left
            self.right = right
            self.width = 0
            if left:
                self.start_col = left.start_col
                self.end_col = left.end_col
                self.width = left.width
            if right:
                self.end_col = right.end_col
                self.width += right.width

    class Group(Node):
        def __str__(self):
            return f'({self.left})'

    class Or(Node):
        def __init__(self, left, right):
            super().__init__(left, right)
            if left.width != right.width:
                raise RuntimeError(f'width of arguments in | do not match ({left.width} | {right.width})')
            self.width = left.width

        def __str__(self):
            return f'{self.left}|{self.right}'

    class Excl(Node):
        def __init__(self, left, right):
            super().__init__(left, right)
            if left.width != right.width:
                raise RuntimeError(f'width of arguments in ! do not match ({left.width} ! {right.width})')
            self.width = left.width

        def __str__(self):
            return f'{self.left}!{self.right}'

    class Cat(Node):
        def __str__(self):
            return f'{self.left}{self.right}'

    class Bits(Node):
        def __init__(self, left=None, right=None, token=None):
            super().__init__(None, None)
            if token:
                self._init_from_token(token)
            else:
                self._init_from_bits(left, right)

        def _init_from_token(self, token):
            if token.char == '0':
                self.bits = '0'
            elif token.char == '1':
                self.bits = '1'
            else:
                self.bits = '-'
            self.start_col = token.col
            self.end_col = token.col
            self.width = 1

        def _init_from_bits(self, left, right):
            self.bits = left.bits + right.bits
            self.start_col = left.start_col
            self.end_col = right.end_col
            self.width = left.width + right.width

        def __str__(self):
            return self.bits

    def p_or(self):
        left = self.p_excl()
        if self.take('|'):
            return PatternParser.Or(left, self.p_or())
        else:
            return left

    def p_excl(self):
        left = self.p_cat()
        while self.take('!'):
            left = PatternParser.Excl(left, self.p_cat())
        return left

    def p_cat(self):
        left = self.p_bit()
        if self.tokens and self.tokens[0].char not in ['|', '!', ')']:
            return PatternParser.build_cat(left, self.p_cat())
        else:
            return left

    def p_bit(self):
        bit = self.take(list('01x-'))
        if bit:
            return PatternParser.Bits(token=bit)
        elif self.take('('):
            start_col = self.current_token.col
            expr = PatternParser.Group(self.p_or(), None)
            if not self.take(')'):
                raise RuntimeError("syntax error; missing )")
            expr.start_col = start_col
            expr.end_col = self.current_token.col
            return expr
        else:
            if self.current_token:
                raise RuntimeError(f"syntax error: expected 0, 1, x, -, or (; got '{self.current_token.char}'")

    @staticmethod
    def build_cat(left, right):
        if type(left) is PatternParser.Bits:
            if type(right) is PatternParser.Bits and left.end_col + 1 == right.start_col:
                return PatternParser.Bits(left=left, right=right)
            elif (type(right) is PatternParser.Cat and
                  type(right.left) is PatternParser.Bits and left.end_col + 1 == right.left.start_col):
                return PatternParser.Cat(PatternParser.Bits(left, right.left), right.right)
        return PatternParser.Cat(left, right)

    def take(self, char):
        if self.tokens:
            next_char = self.tokens[0].char
            found = next_char in char if isinstance(char, list) else next_char == char
            if found:
                self.current_token = self.tokens.pop(0)
                return self.current_token
        self.current_token = None
        return None

    def expr_to_fields(self, node):
        if node:
            if type(node) is PatternParser.Bits:
                yield node.start_col, node.end_col, node.width
            elif type(node) is PatternParser.Cat:
                yield from self.expr_to_fields(node.left)
                yield from self.expr_to_fields(node.right)
            else:
                yield node.start_col, node.end_col, node.width
