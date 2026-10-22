# Copyright (c) 2019 Ampere Computing. All rights reserved.
# SPDX-License-Identifier: BSD-3-Clause

import inspect
import os
import ampere.xlat as xlat
import yaml


def uop_import_arguments(parser):
    parser.add_argument('--uops', metavar='YAML', action='append', default=[], help='Uop definition file(s) (YAML)')


def load_uops(args):
    unit_defaults, uops = load_and_merge_yaml(args.uops)

    uopcodes = {}
    for mnemonic, defn in uops.items():
        with xlat.XlatContext(f'importing uop {mnemonic}'):
            name = mnemonic.replace('.', '_')

            if 'unit' in defn and defn['unit'] in unit_defaults:
                defn_copy = unit_defaults[defn['unit']].copy()
                defn_copy.update(defn)
            else:
                defn_copy = defn.copy()

            new_uop_type = import_uop(name, defn_copy)

            if not inspect.isclass(new_uop_type) or not issubclass(new_uop_type, xlat.Uop):
                raise xlat.XlatError('uop import did not return a subclass of Uop')

            if not type(new_uop_type.opcode) is xlat.Signal:
                raise xlat.XlatError(f'uop "{name}" has an opcode which is not a Signal (bad import_uop behavior?)')

            new_uop_type.name = name

            globals()[name] = new_uop_type
            uopcodes[name] = new_uop_type

    if not uopcodes:
        raise xlat.XlatError('no uops defined (use --uops <YAML file>)')

    return uopcodes


def import_uop(name, defn):
    """Import a Uop class from a YAML definition."""
    opcode_width = require_field(name, defn, 'opcode_width')
    opcode = require_field(name, defn, 'opcode')

    opcode_mask = defn.get('opcode_mask', None)

    new_uop_class = type(name, (xlat.Uop,), defn)
    new_uop_class.opcode = xlat.Signal(opcode_width, opcode, opcode_mask)
    return new_uop_class


def require_field(name, defn, field):
    try:
        return defn[field]
    except KeyError:
        raise xlat.XlatError(f'uop "{name}" is missing required field "{field}"')


def load_and_merge_yaml(uops):
    return merge_yaml(load_yaml(uops))


def load_yaml(uops):
    uop_yaml = []

    for uop_file in uops:
        if not os.path.exists(uop_file):
            raise xlat.XlatError(f'No such file or directory: {uop_file}')

        with open(uop_file) as uf:
            try:
                uy = yaml.load(uf, yaml.CLoader)
            except AttributeError:
                uy = yaml.load(uf)

            unexpected_keys = set(uy.keys()) - {'unit_defaults', 'uops'}

            if unexpected_keys:
                raise xlat.XlatError(f'unexpected key(s) {", ".join(sorted(unexpected_keys))} in {uop_file}')

            uop_yaml.append((uy, uop_file))

    return uop_yaml


class DuplicateKeys(RuntimeError):
    def __init__(self, duplicates):
        super().__init__()
        self.duplicates = duplicates


def merge_yaml(uop_yaml):
    unit_defaults = {}
    uops = {}

    duplicates = []

    for uy, uop_file in uop_yaml:
        for k, dict in (('unit_defaults', unit_defaults), ('uops', uops)):
            if k in uy:
                try:
                    update_disallowing_duplicates(dict, uy[k])
                except DuplicateKeys as dups:
                    duplicates.extend(generate_duplicate_errors(k, dups.duplicates, uop_yaml, uop_file))

    if duplicates:
        raise xlat.XlatError('duplicate keys in uop definitions (described below)\n  ' + '\n  '.join(duplicates))

    return unit_defaults, uops


def update_disallowing_duplicates(d1, d2):
    duplicate_keys = set(d1.keys()) & set(d2.keys())

    if duplicate_keys:
        raise DuplicateKeys(duplicate_keys)

    d1.update(d2)


def generate_duplicate_errors(top_key, dups, uop_yaml, found_in_file):
    errors = []

    for uy, uop_file in uop_yaml:
        if uop_file == found_in_file:
            break
        if top_key not in uy:
            continue
        for dup in dups:
            if dup in uy[top_key]:
                errors.append(f'{top_key}.{dup} from {found_in_file}; first defined in {uop_file}')

    return errors


xlat.initialize(load_uops, arguments=uop_import_arguments)
