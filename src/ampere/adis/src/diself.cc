//
// Copyright (c) 2022 Ampere Computing. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause
//

#if defined(__linux__)
# include <elf.h>
// centos6 doesn't have EM_AARCH64
# if !defined(EM_AARCH64)
#   define EM_AARCH64 183
# endif
#endif
#include <sys/mman.h>
#include <cstdint>
#include <fcntl.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <iostream>
#include <iomanip>
#include <cassert>
#include <cstring>
#include <cstdlib>
#include <map>

#include "disargs.h"
#include "dis-common.h"

static void print_char_until_null(char* p, [[maybe_unused]] void* lim, uint_t n) {
    for(uint_t i=0;i<n;i++) {
        assert(p+i < lim);
        if (p[i] == 0)
            break;
        std::cout << p[i];
    }
    std::cout << '\n';
}
#if defined(__linux__)
static void init_symbol_table(symbols_t& symbols, Elf64_Sym* symtab, std::uint64_t symtab_sz, char* strtab, std::uint64_t strtab_sz, int verbose ) {
    char* lim = strtab + strtab_sz;
    std::uint64_t n = symtab_sz / sizeof(Elf64_Sym);
    if (verbose)
        std::cout <<  "# of symbol table entries: " << n << '\n';
    for (uint_t i = 0 ;i < n; i++) {
        if (verbose) {
            std::cout << i << ":"
                      << "name: " << std::setw(4) << symtab[i].st_name << ' '
                      << " value: " << std::setw(16) << std::hex << std::setfill('0') << symtab[i].st_value << ' ' << std::dec << std::setfill(' ')
                      << " size: " << std::setw(8) << symtab[i].st_size
                      << " section: " << std::setw(8) << symtab[i].st_shndx
                      << '\n';
            print_char_until_null(strtab + symtab[i].st_name, lim, 100);
        }
        symbols[ symtab[i].st_value ] = std::string(strtab + symtab[i].st_name);
    }
}
#endif

void dump(std::uint8_t* f, uint_t n, std::uint64_t va) {
    // dump with low byte on left... which is backwards for itext view
    std::cout << std::hex << std::setfill('0');
    for(uint_t i=0;i<n;i++) {
        if (i>0) {
            if (i%16 == 0)  std::cout << '\n';
            else if (i%4 == 0)  std::cout << " ";
        }
        if (i%16 == 0)
            std::cout << std::setw(16) << va + i << ": ";
        std::cout << std::setw(2) << (int) f[i];
    }
    std::cout << '\n' << std::dec << std::setfill(' ');
}


[[maybe_unused]] static void dump_with_symbols(symbols_t& symbols, std::uint8_t* f, uint_t n, std::uint64_t va) {
    // dump with low byte on left... which is backwards for itext view
    std::cout << std::hex << std::setfill('0');
    uint_t j = 0;
    for(uint_t i=0;i<n;i++) {
        std::uint64_t pc = va + i;
        auto nm = symbols.find(pc);
        if (nm != symbols.end()) {
            std::cout << "\n\n<" << nm->second << ">:\n";
            std::cout << std::setw(16) << pc << ": ";
            j=0;
        }
        if (j>0) {
            if (j%16 == 0)
                std::cout << '\n';
            else if (j%4 == 0)
                std::cout << " ";
            if (j%16 == 0)
                std::cout << std::setw(16) << pc << ": ";
        }
        std::cout << std::setw(2) << (int) f[i];
        j++;
    }
    std::cout << '\n' << std::dec << std::setfill(' ');
}

#if defined(__linux__)
static int read_elf(const args_t& args, std::uint8_t* f, [[maybe_unused]] void* end, [[maybe_unused]] uint_t size) {
    int verbose = 0;

    Elf64_Ehdr* hdr = reinterpret_cast<Elf64_Ehdr*>(f);

    if (hdr->e_ident[0] != ELFMAG0 &&
        hdr->e_ident[1] != ELFMAG1 &&
        hdr->e_ident[2] != ELFMAG2 &&
        hdr->e_ident[3] != ELFMAG3) {
        std::cout << "not an elf file...\n";
        return -1;
    }

    if (hdr->e_machine != EM_AARCH64) {
        std::cout << "not an ARM64 file...\n";
        return -1;
    }

    if (verbose)
        std::cout << "sections = " << hdr->e_shnum << '\n';

    Elf64_Shdr* shdr = reinterpret_cast<Elf64_Shdr*> (f + hdr->e_shoff);
    assert(shdr < end);

    // the section headers have strings in their own section
    if (verbose)
        std::cout << "string table section for header sections " << hdr->e_shstrndx << '\n';
    Elf64_Shdr* section_strings_hdr  = shdr+hdr->e_shstrndx;
    char* section_strings = reinterpret_cast<char*> (f + section_strings_hdr->sh_offset);
    assert(section_strings  < end);
    char* section_strings_lim [[maybe_unused]]= section_strings + section_strings_hdr->sh_size;
    assert(section_strings_lim  < end);

    Elf64_Sym* symtab  = 0;
    char* strtab  = 0;
    std::uint64_t symtab_sz = 0;
    std::uint64_t strtab_sz = 0;
    for(uint_t i=0 ; i < hdr->e_shnum; i++ ) {
        Elf64_Shdr* section = shdr+i;
        char* secname = section_strings + section->sh_name;
        if (verbose) {
            std::cout << "section "  << i << " [" << secname << "]\n";
            if (section->sh_type == SHT_PROGBITS)
                std::cout << "\tprogbits\n";
            if (section->sh_flags & SHF_EXECINSTR)
                std::cout << "\texecinstr\n";
        }

        if (section->sh_type == SHT_SYMTAB) {
            symtab = reinterpret_cast<Elf64_Sym*>(f + section->sh_offset);
            symtab_sz = section->sh_size;
            if (verbose)
                std::cout << "\tsymbol table size (bytes) = " << symtab_sz << '\n';

        }
        else if (section->sh_type == SHT_STRTAB) {
            if (strcmp(secname,".strtab") == 0) {
                strtab = reinterpret_cast<char*>(f + section->sh_offset);
                strtab_sz = section->sh_size;
                if (verbose)
                    std::cout << "\tstrtab size (bytes) = " << strtab_sz << '\n';
            }
        }
    }


    symbols_t symbols;
    init_symbol_table(symbols, symtab, symtab_sz, strtab, strtab_sz, verbose);

#if 0
    for (auto it = symbols.cbegin() ; it != symbols.cend() ; it++) {
        std::cout << "SYMBOLS "
                  << std::hex << std::setfill('0')
                  << std::setw(16) << it->first
                  << " -> "
                  << it->second
                  << '\n';
    }
#endif

    for(uint_t i=0 ; i < hdr->e_shnum; i++ ) {
        Elf64_Shdr* section = shdr+i;
        if (verbose)
            std::cout << "section "  << i << " ...\n";

        if (section->sh_flags & SHF_EXECINSTR) {
            std::uint8_t* itext = f + section->sh_offset;
            uint_t sec_sz = section->sh_size;
            assert(itext + sec_sz <= end);
            //dump(itext,sec_sz, section->sh_addr);
            //dump_with_symbols(symbols,itext, sec_sz, section->sh_addr);
            dump_instr(args, symbols, itext, sec_sz, section->sh_addr);
        }
    }
    return 0;
}
#endif

int main(int argc, char** argv) {
    args_t args = parse_args(argc,argv);
    struct stat s;
    int fd = open(args.input_file.c_str(),O_RDONLY);
    if (fd == -1) {
        std::cerr << "Could not open input file: " << args.input_file << "\n";
        exit(EXIT_FAILURE);
    }
    int status [[maybe_unused]]= fstat(fd, &s);
    assert(status == 0);

    uint_t  size = s.st_size;
    std::uint8_t* f = static_cast<std::uint8_t*>(mmap(0, size, PROT_READ, MAP_PRIVATE, fd, 0));
    void* end = f + size;
    assert(f != 0);
#if defined(__linux__)
    int r = read_elf(args,f,end,size);
#else
    std::cout << "This only runs on Linux.\n";
    int r = -1;
    (void) f; (void)end; (void)size;
#endif
    return r;
}
