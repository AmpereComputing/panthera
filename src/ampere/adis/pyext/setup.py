# Copyright (c) 2022 Ampere Computing. All rights reserved.
# SPDX-License-Identifier: BSD-3-Clause

from setuptools import setup, Extension
import platform

def extra_args():
    extra_compile_args = ['-std=c++17']
    if platform.system() == 'Darwin':
        # this is required for some C++17 features like std::optional.
        # without this option, apple clang refuses to compile code
        # givin an error that  std:optional was introduced on 10.13.
        extra_compile_args += ['-mmacosx-version-min=10.14']
    return extra_compile_args

module1 = Extension('adis',
                    include_dirs       = ["../install/include/adis", "../build"],
                    libraries          = ["adis"],
                    library_dirs       = ['../install/lib'],
                    sources            = ['adis-pyext.cc'],
                    extra_compile_args = extra_args(),
                    language           = 'c++')

setup(name        = "adis",
      version     = "1.0",
      ext_modules = [module1])
