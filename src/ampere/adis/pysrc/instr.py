#!/usr/bin/env python3
#
# Copyright (c) 2022-2024 Ampere Computing. All rights reserved.
# SPDX-License-Identifier: BSD-3-Clause
#
"""
Encoding, instruction, operands and properties support
"""
from __future__ import annotations

import collections
import itertools
import re
import sys

import encoding
import field
import util


def dict_str(d: dict, sep: str = ' ') -> str:
    """Simple string creator for dicts[str]->str"""
    return sep.join([f'{k}:{v}' for k,v in d.items()])

# \D = non-digit, equivalent to [^0-9]
_SUB_CONCAT_PATTERN = re.compile(r'[(](?P<name1>\D\w*):(?P<name2>\D\w*)[)]')
_CONCAT_PATTERN2    = re.compile(r'(?P<name1>\D\w*):(?P<name2>\D\w*)')
_CONCAT_PATTERN3    = re.compile(r'(?P<name1>\D\w*):(?P<name2>\D\w*):(?P<name3>\D\w*)')
# Allow optional quotes in concatenated numeric fields.
_CONCAT_PATTERN2n   = re.compile(r"(?P<name1>[']?\d+[']?):(?P<name2>\D\w*)")
_LDSTN_PATTERN      = re.compile(r'(LD|ST)(?P<nregs>[1234])')
_LDSTN_ESIZE_PATTERN= re.compile(r'integer esize = (?P<esize>\d+);')
_LDSTN_MSIZE_PATTERN= re.compile(r'integer msize = (?P<msize>\d+);')
def _codeify_input(s:str) -> str:
    try:
        m = util.search_subfield_specifier(s)
        if m:
            fld = m.group('field')
            hibit = int(m.group('hibit'))
            lobit = int(m.group('lobit'))
            mask = (1<<(hibit - lobit + 1))-1
            return f'(({fld}>>{lobit}) & 0x{mask:x})'
        m = util.search_onebit_specifier(s)
        if m:
            fld = m.group('field')
            bit = m.group('bit')
            return f'(({fld}>>{bit}) & 1)'
    except ValueError as v:
        print(f"ERROR: processing {s}: {v}", file=sys.stderr)
        assert 0
    return s

class table_t:
    """Support machine-readable operand tables used for C++ generation."""
    def __init__(self,
                 header: list[ (str,str) ],
                 row_data: list[list[str] ],
                 symbol_text: str,
                 other_required_fields:list = None,
                 idstr: str = None):
        self.unique_name:str = None # assigned later after uniquifying names
        if other_required_fields:
            assert isinstance(other_required_fields,list) 
            self.other_required_fields = other_required_fields
        else:
            self.other_required_fields = []
        self.idstr: set[str] = set([]) # for debugging (which instr use this table)
        if idstr:
            self.add_idstr(idstr)
        self.symbol_text = symbol_text
        # the header is typically something like:
        # <entry class="bitfield">size</entry>
        # <entry class="bitfield">Q</entry>
        # <entry class="symbol">&lt;T&gt;</entry>

        # Note: MSR_imm has TWO output columns <pstatefield> and "Architectural Feature"

        self.header: list[ (str,str)] = header
        # the data rows are just the entry.tex field without the "bitfield" or "symbol" qualifiers:
        # <entry class="bitfield">00</entry>
        # <entry class="bitfield">0</entry>
        # <entry class="symbol">8B</entry>
        # one list per row
        self.row_data: list[list[str]] = row_data
        self.output_column = self._find_column()
        self.row_dict, self.input_bit_widths = self._bake_row_data()
        self.index_fields = [ x for _,x in self.header[:self.output_column] ]
        self.target_symbol = self.header[-1][1]
        self.table_type  = self.classify_table_type()

    def get_table_type(self) -> str:
        return self.table_type
    
    def add_other_required_fields(self, x:list):
        self.other_required_fields.extend(x)

    def add_idstr(self, new_id):
        if new_id:
            self.idstr.add(new_id)
    def extend_idstr(self, new_id):
        if new_id:
            self.idstr |= new_id

    def __eq__(self,other):
        if self.symbol_text == other.symbol_text:
            if self.target_symbol == other.target_symbol:
                if self.index_fields == other.index_fields:
                    if self.row_dict == other.row_dict:
                        return True
        return False

    def _find_column(self) -> int:
        for i,(clss,name) in enumerate(self.header):
            if clss == 'symbol' and name == self.symbol_text:
                return i
        return -1

    def _bake_row_data(self) -> (dict[str,str], list[int]):
        """Return a dictionary where values are the output expressions
        and indices are colon seprate groups of bits. The output
        expressions are very varied. Some are letters to indicate
        register classes. Others are constants or other variable
        expressions. We also have "reserved", "absent", "omitted" and
        "present". There are also b.cond conditions.  The other list
        returned is a list of the input bit widths"""
        odict = {}
        last_input_bit_widths = None
        input_bit_widths = None
        for row in self.row_data:
            indices = row[:self.output_column]
            input_bit_widths = [len(x) for x in indices]
            if last_input_bit_widths:
                assert input_bit_widths == last_input_bit_widths
            else:
                last_input_bit_widths = input_bit_widths
            data = row[self.output_column]  # FIXME skipping secondary output column for MSR_imm
            igroups = [ util.splatter_binary(x) for x in indices ]
            for grp in itertools.product(*igroups):
                concat = ":".join(grp)
                odict[concat] = data
        return odict, input_bit_widths

    def get_unique_name(self):
        return self.unique_name
    def set_unique_name(self,s:str):
        self.unique_name = s

    def make_unique_key(self):
        def codeify_input(s:str) -> str:
            m = util.search_subfield_specifier(s)
            if m:
                fld = m.group('field')
                hibit = m.group('hibit')
                lobit = m.group('lobit')
                return f'{fld}_{hibit}to{lobit}'
            m = util.search_onebit_specifier(s)
            if m:
                fld = m.group('field')
                bit = m.group('bit')
                return f'{fld}_{bit}'
            return s

        def codeify_output(s:str) -> str:
            t = re.sub(r'[<>]','',s)
            t = re.sub(r'[|]','_or_',t)
            return t

        inputs = "_".join([codeify_input(x) for x in self.index_fields])
        key = f'{codeify_output(self.symbol_text)}_from_{inputs}'
        return key

    def get_inputs(self) -> list[str]:
        """Return the inputs for the table. Used for generating the
        function definition and for calling it."""
        if self.other_required_fields:
            return self.other_required_fields
        return self.index_fields

    def set_inputs(self, new_inputs:list[str]):
        assert not self.other_required_fields
        self.index_fields = new_inputs

    @staticmethod
    def _fixup(x:str) -> str:
        out = re.sub(r'<','_b',x)
        out = re.sub(r':','to',out)
        out = re.sub(r'>','',out)
        return out

    def classify_table_type(self):
        # this omits B/T, Z/M
        def single_letter_test(x) -> bool:
            return (x in 'WXVdmnBHSDQt' and len(x)==1) or x == 'RESERVED'
        single_letter = all([ single_letter_test(x) for x in self.row_dict.values()])
        if single_letter:
            return 'single letter'

        def present_absent_test(x) -> bool:
            return x == '[present]' or x == '[absent]'
        present_absent  = all([ present_absent_test(x) for x in self.row_dict.values()])
        if present_absent:
            return 'present/absent'

        def functional_test(x) -> bool:
            return (':' in x or '(' in x) and x != '(omitted)'
        functional  = any([ functional_test(x) for x in self.row_dict.values()])
        if functional:
            return 'functional'
        return 'generic string'

    def make_test(self, argnames:list[str], indx:str) -> str:
        indx_chunks = indx.split(sep=':')
        if  len(indx_chunks) != len(argnames):
            util.msge(f"ARG MISMATCH {argnames} vs {indx_chunks}")
        assert len(indx_chunks) == len(argnames)
        tests = []
        for arg, indchk in zip(argnames, indx_chunks):
            tests.append(f'{arg} == 0b{indchk}')
        return "&&".join(tests)

    def make_test_tuple(self, argnames:list[str], indx:str) -> str:
        indx_chunks = indx.split(sep=':')
        if  len(indx_chunks) != len(argnames):
            util.msge(f"ARG MISMATCH {argnames} vs {indx_chunks}")
        assert len(indx_chunks) == len(argnames)
        tests = [f'0b{indchk}' for indchk in indx_chunks]
        tup = ",".join(tests)
        return f'{{{tup}}}'

    def make_index_value(self):

        coded_fields = [_codeify_input(infield) for infield in self.index_fields] #self.get_inputs()]
        # reverse the bit widths, do prefix sum, reverse that, remove first element and tack on a zero.
        # rightmost chunk shifts by 0. 
        shift_vals = list(reversed(list(itertools.accumulate(reversed(self.input_bit_widths)))))[1:] + [0]
        mask_vals = [ ((1<<x)-1) for x in self.input_bit_widths]
        chunks  = [f'(({x}&{m})<<{y})' for x,y,m in zip(coded_fields, shift_vals, mask_vals)]
        s = "|".join(chunks)
        return f'({s})'


    def codegen_oval(self, oval) -> [str]:
        _FLD_WIDTH = {'1':1, '0':1, 'H':1, "L":1, "M":1, 'Rm':5, 'immb':3} #FIXME: derive this. hack..
        def get_width(s:str) -> int:
            return _FLD_WIDTH[s]

        p =  _SUB_CONCAT_PATTERN.search(oval)
        if p:
            name1, name2 = p.group('name1', 'name2')
            replace = f'{name1}:{name2}'
            n = get_width(name2)
            new = f'(({_codeify_input(name1)}<<{n})|{_codeify_input(name2)})'
            x = re.sub(replace,new,oval)
            x = re.sub(r'uint','',x, flags=re.IGNORECASE) # UInt and Uint are both used! 
            return [f'    adis_vdat_imm(vdis,{x}); //1']

        p = _CONCAT_PATTERN3.search(oval)
        if p:
            name1, name2, name3 = p.group('name1', 'name2', 'name3')
            name1_no_paren = re.sub(r'[(]', '', name1)
            m = get_width(name3)
            n = get_width(name2) + m
            return [f'    adis_vdat_imm(vdis, ({_codeify_input(name1_no_paren)}<<{n})|({_codeify_input(name2)}<<{m})|({_codeify_input(name3)})); //3 {name1=}, {name2=}, {name3=}, {m=}, {n=}']

        p = _CONCAT_PATTERN2n.search(oval)
        if p:
            name1, name2 = p.group('name1', 'name2')
            name1 = re.sub(r"[']",'', name1)
            n = get_width(name2)
            return [f'    adis_vdat_imm(vdis, ({name1}<<{n})|{_codeify_input(name2)}); //2n']

        p = _CONCAT_PATTERN2.search(oval)
        if p:
            name1, name2 = p.group('name1', 'name2')
            n = get_width(name2)
            return [f'    adis_vdat_imm(vdis, ({_codeify_input(name1)}<<{n})|{_codeify_input(name2)}); //2']

        if oval == 'RESERVED':
            return [ '    adis_vdat_RESERVED(vdis);' ]
        elif oval == 'SEE Advanced SIMD modified immediate': # should never happen, decodes to something else
            return [ '    adis_vdat_RESERVED(vdis);' ] 

        oval = re.sub(r'uint', '', oval, flags=re.IGNORECASE)
        out = [f'    adis_vdat_imm(vdis, {_codeify_input(oval)}); //final']
        return out

    def gen_functional_table(self, arg_names:list[str], table_rhs_to_enum: dict[str,str]) -> list[str]:
        def cleanup_idx(s):
            return re.sub(r':','',s)

        olist = ['// FIXME: write me - functional']
        olist += [f'const std::uint32_t v = {self.make_index_value()};']
        olist += ['switch(v) {']
        for indx,oval in self.row_dict.items():
            olist += [ f'case 0b{cleanup_idx(indx)}:  // {indx} -> {oval}']
            olist += self.codegen_oval(oval)
            olist += [ '    break;']
        olist += [' default: break;']
        olist += ['}']
        return olist

    def emit_strtab_map(self, tbl: dict[str,set], tup_type: str, tup_arg: str) -> list[str]:
        """Emit a static map from tuples of field values to a strtab value and a lookup function. """
        olines = []
        olines.append(f'static std::map<{tup_type}, adis_strtab_enum_t> tmap = {{')
        for key, tups in  tbl.items():
            for tup in tups:
                olines.append(f'{{ {tup}, {key} }},')
        olines.append('};')
        r = f'''
if (auto search = tmap.find( {tup_arg} ); search != tmap.end()) {{
   adis_vdat_strtab(vdis, search->second);
}}'''
        olines.append(r)
        return olines
    def gen_function_body(self, arg_names:list[str], table_rhs_to_enum: dict[str,str]) -> list[str]:
        """Create the actual table lookup function body (minus fn
        head/tail). Return a list of strings of code. The arg_names
        are the names of the formal arguments passed in by the caller
        table_rhs_to_enum is a dict mapping operand-table outputs to
        strtab enum names."""
        olist = []
        olist.append(f'// table_type = {self.table_type}')
        if self.table_type in ['single letter', 'generic string']:
            if 1:
                # build a mapping from ADIS_STRTAB_* values to the tuples of input args that map to that value
                oval_sets = collections.defaultdict(set)
                for i, (indx, oval) in enumerate(self.row_dict.items()):
                    tup = self.make_test_tuple(arg_names, indx)
                    strname = table_rhs_to_enum[oval]
                    str_enum = f'ADIS_STRTAB_{strname}'
                    oval_sets[str_enum].add(tup)
                tup_arg = '{' + ",".join(arg_names) + '}' # used to lookup stuff in the map
                x = ",".join(['std::uint32_t']*len(arg_names)) # used to create the std::map
                tup_type = f'std::tuple<{x}>'
                olist = self.emit_strtab_map(oval_sets, tup_type, tup_arg)
            else:
                # disabled/replaced this (with the above) because it was generating
                # warnings from clang compilations for very long
                # functions. 2025-04-21
                for i, (indx, oval) in enumerate(self.row_dict.items()):
                    test = self.make_test(arg_names, indx)
                    var_if = 'if' if i == 0 else 'else if'
                    strname = table_rhs_to_enum[oval]
                    olist += [f'{var_if} ({test}) {{ adis_vdat_strtab(vdis, ADIS_STRTAB_{strname}); }}']
        elif self.table_type == 'present/absent':
            for i,(indx,oval) in enumerate(self.row_dict.items()):
                test = self.make_test(arg_names,indx)
                var_if = 'if' if i == 0 else 'else if'
                olist += [f'{var_if} ({test}) {{ adis_vdat_{oval}(vdis); }}']

        elif self.table_type == 'functional':
            olist += self.gen_functional_table(arg_names, table_rhs_to_enum)
        #elif self.table_type == 'generic string':
        #    for i, (indx, oval) in enumerate(self.row_dict.items()):
        #        test = self.make_test(arg_names, indx)
        #        var_if = 'if' if i == 0 else 'else if'
        #        strname = table_rhs_to_enum[oval]
        #        olist += [f'{var_if} ({test}) {{ adis_vdat_strtab(vdis, ADIS_STRTAB_{strname}); }}']
        else:
            assert False
        return olist



    def emit_function(self, table_rhs_to_enum: dict[str,str]) -> (str,str):
        """Return prototype and function body"""

        arg_names = [table_t._fixup(x) for x in self.get_inputs()]
        args = ",".join([f'std::uint32_t {x}' for x in arg_names])

        func =  [ f'// used by: {x}' for x in self.idstr]
        proto = f'void adis_vdat_{self.unique_name}(adis_vdata_t* vdis, {args})'
        func += [ proto ]
        func += ['{']
        for indx,oval in self.row_dict.items():
            func += [ f'// {indx} -> {oval}']
        func.extend(self.gen_function_body(arg_names, table_rhs_to_enum)) # create the actual lookup
        func.extend([f'(void){x};' for x in arg_names])
        func += ['(void)vdis;']
        func += ['}']
        return proto + ';', "\n".join(func)

    def __str__(self):
        difft = ''
        if self.other_required_fields and self.index_fields != self.other_required_fields:
            difft='DIFFT'

        s = [f'{self.index_fields} {self.other_required_fields} {difft} -> {self.target_symbol}  symbol_text: {self.symbol_text}  uname: {self.unique_name}']
        if self.idstr:
            s.append(f"IDSTR: {','.join(self.idstr)}")
        for k,v in self.row_dict.items():
            s.append(f'{k} -> {v}')
        return "\n".join(s)


def create_cond_table() -> table_t:
    def mk_hdr_row( a:str, b:str ) -> list[(str,str)]:
        return [('bitfield',a), ('symbol',b)]
    def mk_row( a:str, b:str ) -> list[str]:
        return [ a, b ]

    header = mk_hdr_row('cond','<cond>')
    data = []
    data.append( mk_row('0000','eq') )
    data.append( mk_row('0001','ne') )
    data.append( mk_row('0010','cs') )
    data.append( mk_row('0011','cc') )
    data.append( mk_row('0100','mi') )
    data.append( mk_row('0101','pl') )
    data.append( mk_row('0110','vs') )
    data.append( mk_row('0111','vc') )

    data.append( mk_row('1000','hi') )
    data.append( mk_row('1001','ls') )
    data.append( mk_row('1010','ge') )
    data.append( mk_row('1011','lt') )
    data.append( mk_row('1100','gt') )
    data.append( mk_row('1101','le') )
    data.append( mk_row('1110','al') )
    data.append( mk_row('1111','nv') ) # same as "al"

    t = table_t(header, data, '<cond>')
    return t

class explanation_t:
    """Operand explanation and optional value table."""
    _ecount = 0

    def __init__(self, encoded_in:list, intro: list[str], table: table_t = None):
        self.eid = explanation_t._ecount
        explanation_t._ecount += 1
        # a string describing the colon-separated fields that this explanation pertains to.
        assert isinstance(encoded_in,list)
        self.encoded_in: list = encoded_in
        self.intro: list[str] = intro

        # for "definition" nodes, not "account" nodes
        # except I add condition-code tables for <cond> symbols.
        self.table: table_t = table
        if table:
            print("TABLE", table, file=util.dbg_file())

    def get_pimm_scale(self) -> int:
        m  = re.search(r'<pimm>/(?P<scale>\d+)', "\n".join(self.intro))
        if m:
            return int(m.group('scale'))
        return 1

    def __str__(self):
        s = [ f'EXPLANATION self.eid={self.eid} self.encoded_in={self.encoded_in}' ]
        s.append( 'INTRO: ' + self.get_intro_str() )
        s.append( f'TABLE: {self.table}' )
        return "\n".join(s)

    def get_intro(self):
        return self.intro
    def get_intro_str(self):
        return " ".join(self.intro)
    def simple(self):
        return not self.table
    def get_table(self):
        return self.table
    def remove_table(self):
        self.table = None
    def get_encoded_in(self) -> list:
        return self.encoded_in
    def get_encoded_in_as_str(self) -> str:
        return ":".join(self.encoded_in)
    def set_encoded_in(self,v : list):
        assert isinstance(v,list)
        self.encoded_in = v

    def intro_as_string(self) -> str:
        return " ".join(self.intro)

class asm_operand_t:
    """One token in an instruction assembly pattern."""
    def __init__(self, text:str, hover:str = None, link:str = None,
                 last_was_space:bool = False,
                 register_prefix:bool = False,
                 is_load:bool = False):
        self.is_load = is_load
        self.text: str  = text
        self.hover: str = hover
        self.link: str  = link  # these link to the explanations info
        self.string_enum = None  # assigned later after string table created
        if text.startswith('{') and not last_was_space:
            self.omit_subsequent_curly_brackets = True
        else:
            self.omit_subsequent_curly_brackets = False
        #print(f"\t{self.omit_subsequent_curly_brackets=}",file=sys.stderr)
        if register_prefix:
            self.operand_type = "register-prefix" # scalar shifts, etc.
        elif not link:
            self.operand_type = "punctuation"
        elif re.search(r'<\w+>', self.text):
            self.operand_type = "variable field"
            if 'SIMD&FP' in self.hover:
                self.operand_type = 'vfp'
            elif re.search(r'general-purpose.* register', self.hover, re.IGNORECASE):
                self.operand_type = 'gpr'

        elif re.search(r'<X\w+[|]SP>', self.text):
            self.operand_type = "variable reg 64b or SP"
        elif re.search(r'<n[|]SP>', self.text):
            self.operand_type = "variable reg or SP"
        elif re.search(r'<W\w+[|]WSP>', self.text):
            self.operand_type = "variable reg 32b or WSP"
        elif re.search(r'<[XW][(][st][+][123][)]>', self.text):
            self.operand_type = "implied incrmented gpr"
        elif re.search(r'<Vn[+][123]>', self.text):
            self.operand_type = "implied incrmented vector reg"
        elif self.text == "{2}":
            self.operand_type = "optional 2 suffix"
        else:
            self.operand_type = "surprise!"

    def get_type(self) -> str:
        return self.operand_type
    def is_punctuation(self) -> bool:
        return self.get_type() == 'punctuation'
    def __str__(self) -> str:
        return self.text
    def set_text(self, s:str):
        self.text = s
    def get_text(self, filter_curly_brackets=False) -> str:
        if 1:
            if filter_curly_brackets:
                return re.sub(r'[{}]','',self.text)
        return self.text
    def get_link(self) -> str:
        return self.link
    def get_hover(self) -> str:
        return self.hover

    def signed_imm(self) -> bool:
        hvr = self.get_hover()
        if re.search('signed imm', hvr, re.IGNORECASE) and not re.search('unsigned', hvr, re.IGNORECASE):
            return True
        return False


    def unsigned_imm(self) -> bool:
        hvr = self.get_hover()
        if re.search('unsigned imm', hvr, re.IGNORECASE):
            return True
        return False


    def upper_predicate_reg_field(self) -> bool:
        """Some predicates have an implicit 1 in their encoding and can only uses P8...P15. They can read or written regs."""
        if self.hover:
            if 'scalable predicate register P8-P15' in self.hover:
                return True
        return False

    def read(self):
        if self.hover:
            if re.search(r'[sS]ource', self.hover):
                return True
            if 'base register' in self.hover:
                return True
            if re.search(r'[gG]overning scalable predicate register',self.hover):
                return True
            if 'register to be transferred' in self.hover:
                if not self.is_load:
                    return True
            if 'register to be compared and loaded' in self.hover:
                return True
            if re.search('register.*stored',self.hover): # so it is read
                return True
            if 'Base scalable vector register' in self.hover:
                return True
            if 'post-index register' in self.hover:
                return True
            if 'accumulator input register' in self.hover:
                return True
        return False

    def written(self):
        if self.hover:
            if re.search(r'[dD]estination',self.hover):
                return True
            if 'register to be transferred' in self.hover:
                if self.is_load:
                    return True
            if 'register to be loaded' in self.hover:
               return True
            if 'register to be compared and loaded' in self.hover:
                return True
            if 'post-index register' in self.hover:
                return True
            if 'accumulator output register' in self.hover:
                return True
            if re.search(r'register into which.*is written',self.hover):
                return True
        # FIXME: pre-index, post-index (docvars)
        return False

    def get_legal_function_root(self) -> str:
        """Clean out special characters to make a C-programming legal
        substring for this operand"""
        t = self.text
        t = re.sub(r'[<>]','',t)
        t = re.sub(r'[|]','_or_',t)
        t = re.sub(r'\{2\}','opt2',t) #no underscore reqd, nothing to separate, first thing
        t = re.sub(r'\(s[+]1\)','s_plus1',t)
        t = re.sub(r'\(t[+]1\)','t_plus1',t)
        t = re.sub(r'[+]1','_plus1',t)
        t = re.sub(r'[+]2','_plus2',t)
        t = re.sub(r'[+]3','_plus3',t)
        return t

    def rw_action_clear(self):
        return self.read() or self.written()
    def get_rw(self) -> list[str]:
        s = []
        if self.rw_action_clear():
            if self.read():
                s.append("read")
            if self.written():
                s.append("written")
        else:
            s.append("read-assumed")
        return s

    def detail(self) -> str:
        s = []
        s.append(f'category={self.operand_type}')
        s.append(f'text={self.text}')
        if self.hover:
            s.append(f'hover={self.hover}')
        if self.link:
            s.append(f'link={self.link}')
        if self.rw_action_clear():
            if self.read():
                s.append("read")
            if self.written():
                s.append("written")
        elif self.operand_type != 'punctuation':
            s.append("read-assumed")
        return " ".join(s)


class asm_instruction_t:
    """The lower level details of an instruction assembly pattern."""
    def __init__(self):
        self.mnemonic: str = None
        self.operands_prototype: str = None
        self.asm_operands: list[asm_operand_t] = []
        self.prototype: str = None # full ARM spaced-out assembler thing

    def add(self, s: asm_operand_t):
        self.asm_operands.append(s)

    def extend(self, ls: list[asm_operand_t]):
        for ao in ls:
            self.asm_operands.append(ao)
    def get_vector_srcs(self) -> int:
        n = 0
        for ao in self.asm_operands:
            if ao.read() and 'vector' in ao.hover:
                n = n + 1
        return n
    def get_mnemonic(self) -> str:
        return self.mnemonic
    def set_mnemonic(self,s:str):
        self.mnemonic = s
    def get_operands(self) -> list[asm_operand_t]:
        return self.asm_operands
    def set_operands(self, oplst: list[asm_operand_t]):
        self.asm_operands = oplst
    def get_prototype(self) -> str:
        return self.prototype
    def bake(self, original_asm_prototype:str):
        """Called after we get all the operands to parse them into actual operands """

        # for now we do nothing since the ARM operands are an rather
        # inconsistent. Sometimes an operand that is a source and dest
        # is listed twice, sometimes it is listed only once.

        self.prototype = original_asm_prototype
        self.mnemonic = re.sub(r'[ ].*$','', self.prototype)
        if self.mnemonic.endswith('.<cond>'):  # for B.<cond> and BC.<cond>
            self.mnemonic = re.sub(r'<cond>','',self.mnemonic) # leave the dot.
        self.operands_prototype = re.sub(r'^[^ ]+[ ]+','', self.prototype)

        if 0:
            new_operands = []
            for op in self.asm_operands:
                new_operands.append(op)
            self.asm_operands = new_operands

    def __str__(self):
        return self.prototype

    def detail(self) -> list[str]:
        return [s.detail() for s in self.asm_operands]



class decorated_encoding_t:
    """This is the main data structure for recoding instruction
    information in the generator.  All the properties about
    instructions come from this object.

    """
    def __init__(self,
                 source_instruction: object,
                 source_class: object,
                 source_encoding: object,
                 enc: encoding.encoding_t,
                 iclass_id: str = '',
                 encoding_name: str = '',
                 attr: dict = None,
                 ainst: asm_instruction_t = None):
        self.source_instruction = source_instruction
        self.source_class = source_class
        self.source_encoding = source_encoding
        self.simd_checks = []
        self.uses_decode_bit_masks_algorithm:bool = False
        self.uses_decode_bit_masks_algorithm_logical:bool = False
        self.enc: encoding.encoding_t = enc
        self.pseudocode = None
        self.operand_pseudocode = None
        self.iclass_id: str = iclass_id
        self.encoding_name: str = encoding_name
        self.attr: dict = attr
        self.ainst: asm_instruction_t = ainst
        self.explanations: dict[str,explanation_t] = None
        self.inum = 0
        self.sys_alias_qualifier: str = None
        self.set_pseudocode()
        self.fix_attr()
        self.vl256_required = self._find_vl256_requirement()
        self.set_category()
        self._set_memop_esize()
        self._set_memop_msize()

        # these are for the operand pattern codegen:
        # filled in later on by genadis._emit_operand_schema() after we collect up all the strings.
        # operand_pattern_id is the group id that identifies the shared sequence this instr uses.
        self.operand_pattern_id: int = 0
        # operand_pattern_offset is the index where disasm should begin looking for the emit sequence
        self.operand_pattern_offset: int = 0

    def set_sys_alias_qualifier(self, qualifier: str):
        self.sys_alias_qualifier = qualifier
    def get_sys_alias_qualifier(self) -> str:
        if self.sys_alias_qualifier:
            return self.sys_alias_qualifier
        return 'INVALID'

    def is_gather(self) -> bool:
        return self.property_gather
    def is_scatter(self) -> bool:
        return self.property_scatter

    def is_sparse(self) -> bool:
        return self.is_gather() or self.is_scatter()
    def is_sparse_unscaled(self) -> bool:
        return 'unscaled' in self.get_encoding_name()
    def is_sparse_scaled(self) -> bool:
        return '_scaled' in self.get_encoding_name()
    def get_sparse_index_sz(self) -> int:
        return 32 if 'x32' in self.get_encoding_name() else 64
    def is_sparse_packed(self) -> bool: 
        return  '_s_' in self.get_encoding_name()
    def is_sparse_unpacked(self) -> bool: 
        return  '_d_' in self.get_encoding_name()
    def is_sve_agen_reg_reg(self) -> bool: # FIXME: only works for SVE
        n = self.get_encoding_name()
        return  '_bz_' in n  or '_br_' in n or '_ar_' in n
    def is_sve_agen_reg_imm(self) -> bool: # FIXME: only works for SVE
        n = self.get_encoding_name()
        return  '_ai_' in n or '_bi_' in n
    def sparse_info(self) -> str:
        s = []
        if self.is_gather():
            s.append("gather")
        elif self.is_scatter():
            s.append("scatter")
        else:
            return "not sparse"
        s.append(self.get_category())
        if self.is_sve_agen_reg_reg():
            s.append("reg-reg")
        if self.is_sve_agen_reg_imm():
            s.append("reg-imm")
        s.append(f'{self.get_sparse_index_sz()}b indices')
        if self.is_sparse_unscaled():
            s.append('unscaled')
        if self.is_sparse_scaled():
            s.append('unscaled')
        if self.is_sparse_packed():
            s.append('packed')
        if self.is_sparse_unpacked():
            s.append('unpacked')
        return ", ".join(s)

    def set_category(self):
        self.property_load = False
        self.property_load_ff = False
        self.property_load_nf = False
        self.property_store = False
        self.property_atomic = False
        self.property_gather = False
        self.property_scatter = False
        self.property_prefetch = False
        self.property_branch = False
        self.property_branch_cond = False
        self.property_branch_indirect = False
        self.property_branch_call = False
        self.property_branch_return = False
        self.property_exception = False
        self.property_gather_prefetch = False
        self.property_sme = False
        self.property_address_translation = False
        self.property_eret = False
        self.property_pc64plus = False  # PC relative branches
        self.property_pc64minus = False # PAC AUT instr

        # The generator's input supplies these decoder properties directly.
        # Keeping the classification on the source record avoids coupling the
        # generated interface to descriptive prose.
        semantic = self.attr.get('semantic')
        if semantic is not None:
            self.category = semantic['category']
            for name, value in semantic.items():
                if name != 'category':
                    setattr(self, f'property_{name}', value)
            return


    def get_category(self):
        return self.category
    def is_sve_family(self):
        """Any of SVE, SVE2, etc."""
        return self.instruction_class() in ['sve', 'sve2', 'sve2p1']
    def is_sve(self):
        """Only SVE, not SVE2, etc."""
        return self.instruction_class() == 'sve'
    def is_sve2(self):
        """Only SVE2, not SVE, etc."""
        return self.instruction_class() == 'sve2'
    def is_sve_simd(self):
        if self.is_sve():
            return '<Z'  in self.ainst.get_prototype()
        return False
    def is_advsimd(self):
        # fpsimd is a bunch of fp-scalar and vector memops.
        # some memops are different category so we use the instruction_class
        if self.instruction_class() in ['advsimd', 'fpsimd']:
            return True
        return self.get_category() == 'advsimd'
    def is_advsimd_simd(self):
        if self.instruction_class() == 'fpsimd':
            return '<Q'  in self.ainst.get_prototype() # exclude the fp-scalar
        if self.is_advsimd():
            return self.instruction_class() != 'float'
        return False
    def is_sme(self):
        """True for SME or SME2"""
        return self.property_sme
    def is_memop(self):
        return (self.is_load() or self.is_store() or self.is_gather() or self.is_scatter()
                or self.is_prefetch() or self.is_gather_prefetch() or self.is_atomic())
    def is_ldst(self):
        return self.is_load() or self.is_store() or self.is_atomic()

    def is_atomic(self):
        return self.property_atomic
    def is_load(self):
        return self.property_load
    def is_load_ff(self):
        return self.property_load_ff
    def is_load_nf(self):
        return self.property_load_nf
    def is_store(self):
        return self.property_store
    def is_pcrel_plus(self):
        return self.property_pc64plus
    def is_pcrel_minus(self):
        return self.property_pc64minus
    def is_prefetch(self):
        return self.property_prefetch
    def is_branch(self):
        return self.property_branch
    def is_branch_cond(self):
        return self.property_branch_cond
    def is_branch_indirect(self):
        return self.property_branch_indirect
    def is_branch_call(self):
        return self.property_branch_call
    def is_branch_return(self):
        return self.property_branch_return
    def is_exception(self):
        return self.property_exception
    def is_address_translation(self):
        return self.property_address_translation
    def is_eret(self):
        return self.property_eret
    def is_gather_prefetch(self):
        return self.property_gather_prefetch

    def is_fp_divide(self): # sve and neon
        return re.search(r'fdiv', self.get_encoding_name(), re.IGNORECASE)
    def is_divide_reversed(self): 
        return re.search(r'divr', self.get_encoding_name(), re.IGNORECASE)
    def is_int_divide(self): # sve and gpr
        return re.search(r'[su]div', self.get_encoding_name(), re.IGNORECASE)
    def is_divide(self):
        return self.is_int_divide() or self.is_fp_divide() or self.is_divide_reversed()
    def is_sqrt(self):
        return re.search(r'[^r]sqrt', self.get_encoding_name(), re.IGNORECASE)
    def is_rsqrt(self):
        return re.search(r'rsqrt', self.get_encoding_name(), re.IGNORECASE)
    def is_ic(self):
        return self.get_mnemonic() == 'IC'
    def is_dc(self):
        return self.get_mnemonic() == 'DC'
    def is_tlbi(self):
        return self.get_mnemonic() == 'TLBI'

    def nregs(self):
        if self.is_ldst():
            if self.category in [ 'load_sve_mult', 'store_sve_mult',
                                  'load_mult', 'store_mult']:
                m  = _LDSTN_PATTERN.search(self.get_mnemonic())
                if m:
                    nregs = int(m.group('nregs'))
                elif self.get_mnemonic() in ['LDAP1', 'STL1']:
                    nregs = 1
                else:
                    print(f"UNEXPECTED NAME: {self.get_mnemonic()}")
                    assert 0
                return nregs
            return 1
        return 0
    def _set_memop_esize(self):
        semantic = self.attr.get('semantic')
        if semantic is not None:
            self.esize = semantic.get('memop_esize', 0)
            return
        m = _LDSTN_ESIZE_PATTERN.search(self.get_pseudocode())
        if m:
            self.esize = int(m.group('esize'))
        else:
            self.esize = 0 # UNKNOWN
    def _set_memop_msize(self):
        semantic = self.attr.get('semantic')
        if semantic is not None:
            self.msize = semantic.get('memop_msize', 0)
            return
        m = _LDSTN_MSIZE_PATTERN.search(self.get_pseudocode())
        if m:
            self.msize = int(m.group('msize'))
        else:
            self.msize = 0 # UNKNOWN

    def get_memop_esize(self) -> int:
        """Return ld/st vector element size in bits"""
        return self.esize
    def get_memop_msize(self) -> int:
        """Return ld/st vector memory size in bits"""
        return self.msize

    def fix_attr(self):
        self.feature = "unspecified"
        if 'feature' in self.attr:
            self.feature = self.attr['feature']
        if self.feature == 'unspecified':
            if self.instruction_class() == 'sve':
                self.feature = 'FEAT_SVE'
            elif self.instruction_class() == 'sve2':
                self.feature = 'FEAT_SVE2'
            elif self.instruction_class() in ['mortlach', 'sme']:
                self.feature = 'FEAT_SME'
            elif self.instruction_class() in ['mortlach2', 'sme2']:
                self.feature = 'FEAT_SME2'
        elif self.feature == 'crc':
            self.feature = 'FEAT_CRC32'

    def uses_program_counter(self):
        return self.attr.get('uses_program_counter', False)

    def may_write_flags(self):
        return self.attr.get('may_write_flags', False)
    def may_read_flags(self):
        return self.attr.get('may_read_flags', False)

    def get_instr_id(self) -> str:
        return self.source_instruction.get('id')
    def get_instr_title(self) -> str:
        return self.source_instruction.get('title')

    def set_uses_decode_bit_masks_algorithm(self):
        self.uses_decode_bit_masks_algorithm = True
    def get_uses_decode_bit_masks_algorithm(self) -> bool:
        return self.uses_decode_bit_masks_algorithm

    def set_uses_decode_bit_masks_algorithm_logical(self):
        self.uses_decode_bit_masks_algorithm_logical = True
    def get_uses_decode_bit_masks_algorithm_logical(self) -> bool:
        return self.uses_decode_bit_masks_algorithm_logical

    def _find_vl256_requirement(self):
        return self.get_feature() == 'FEAT_F64MM'

    def set_pseudocode(self):
        self.operand_pseudocode = ''
        self.pseudocode = ''

    # @functools.cache # requires python >=3.9 
    def get_pseudocode(self) -> str:
        txt = ''
        if self.operand_pseudocode:
            txt = self.operand_pseudocode
        if not txt.endswith('\n'):
            txt += '\n'
        if self.pseudocode:
            txt += self.pseudocode
        return txt

    def has_gpr_dst(self) -> bool:
        for opnd in self.get_operands():
            if opnd.written():
                if opnd.get_text() in ['<Xd>','<Wd>']: # gpr #FIXME move to operand...
                    return True
        return False

    def get_merging_fpcr_nep(self) -> bool:
        return self.attr.get('may_sse_merge', False)
  
    def pre_index(self):  # updates base (has "!" in template)
        if self.attr.get('semantic') is not None:
            return self.property_pre_index
        if self.attr.get('address-form', None) == 'pre-indexed':
            return True
        # 2022-12-29: some instructions (like LDRAA and LDRAB) have
        # pre-index-update forms but do not have it indicated in the
        # address-form docvar. Unfortunately, this check also catches
        # the CPY/SET (FEAT_MOPS) instructions that post-update their
        # base regs so we must handle those specially.
        if ']!' in self.ainst.get_prototype():
            if self.get_feature() == 'FEAT_MOPS':
                return False
            return True
        return False

    def post_index(self):  # updates base (does NOT have "!" in template)
        if self.attr.get('semantic') is not None:
            return self.property_post_index
        if self.attr.get('address-form', None) == 'post-indexed':
            return True
        if ']!' in self.ainst.get_prototype():
            if self.get_feature() == 'FEAT_MOPS':
                return True
        return False


    def set_inum(self, inum):
        self.inum = inum
    def get_inum(self):
        return self.inum

    def set_explanations(self,edict:dict[str,explanation_t]):
        self.explanations = edict
    def add_explanation(self, key:str, exp: explanation_t):
        self.explanations[key] = exp
    def get_explanations(self) -> dict[str,explanation_t]:
        return self.explanations
    def get_explanation(self, link:str) -> str:
        return self.explanations[link]
    def instruction_class(self) -> str:
        if 'instr-class' in self.attr:
            return self.attr['instr-class']
        return self.get_feature() # FIXME: can we do better?
    def predicated(self) -> bool:
        if 'predicated' in self.attr:
            return self.attr['predicated']
        return False
    def dit(self) -> bool:
        """FEAT_DIT - data independent timing"""
        if 'uses_dit' in self.attr:
            return self.attr['uses_dit']
        return False
    def takes_movprfx(self) -> bool:
        if 'takes_movprfx' in self.attr:
            return self.attr['takes_movprfx']
        return False
    def takes_movprfx_pred(self) -> bool:
        if 'takes_pred_movprfx' in self.attr:
            return self.attr['takes_pred_movprfx']
        return False

    def get_feature(self) -> str:
        return self.feature

    def arch_variant(self) -> str:
        return self.attr['arch_variant']

    def get_mnemonic(self) -> str:
        return self.ainst.get_mnemonic()
    def set_mnemonic(self, s):
        self.ainst.set_mnemonic(s)
    def get_operands(self) -> list[asm_operand_t]:
        return self.ainst.get_operands()
    def get_vector_srcs(self) -> int:
        if "vector_srcs" in self.attr:
            return self.attr["vector_srcs"]
        return self.ainst.get_vector_srcs()

    def get_iclass_id(self) -> str:
        return self.iclass_id
    def get_encoding_name(self) -> str:
        return self.encoding_name
    def set_encoding_name(self,s):
        self.encoding_name = s

    def get_rexp(self) -> str:
        """get a string for building DFA in DFA-input syntax"""
        return self.enc.get_rexp()
    def get_enc(self) -> encoding.encoding_t:
        return self.enc

    def asm_string(self) -> str:
        return str(self.ainst)

    def asm_string_details(self) -> list[str]:
        return self.ainst.detail()
