//
// Copyright (c) 2022 Ampere Computing. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause
//


// Headers are here:
//  /Applications/Xcode.app/Contents/Developer/Platforms/MacOSX.platform/Developer/SDKs/MacOSX.sdk/System/Library/Frameworks/Kernel.framework/Versions/A/Headers/mach-o
//  /Applications/Xcode.app/Contents/Developer/Platforms/MacOSX.platform/Developer/SDKs/MacOSX.sdk/usr/include/mach/machine.h

#if defined(__APPLE__)
# include <mach-o/fat.h>
# include <mach-o/loader.h>
# include <mach-o/nlist.h>
# include <mach-o/reloc.h>
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


[[maybe_unused]] static void print_char_until_null(char* p, [[maybe_unused]] void* lim, uint_t n) {
    for(uint_t i=0;i<n;i++) {
        assert(p+i < lim);
        if (p[i] == 0)
            break;
        std::cout << p[i];
    }
    std::cout << '\n';
}


[[maybe_unused]] static void dump(std::uint8_t* f, uint_t n, std::uint64_t va) {
    // dump with low byte on left... which is backwards for itext view
    std::cout << std::hex << std::setfill('0');
    std::uint32_t d=0;
    for(std::uint64_t i=0;i<n;i++) {
        d = d | (f[i] << (i*8));

        if (i%4 == 3) {
            std::cout << std::setw(16) << va + i - 3 << ": " << std::setw(8) << d << '\n';
            d = 0;
        }
    }
    if (d) {
        std::cout << std::setw(16) << "..." << ": " <<  d  << '\n';
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


[[maybe_unused]] static std::uint32_t bswap(std::uint32_t x) {
    return (x>>24) | ((x>>8)&0x0000ff00) | ((x<<8)&0x00FF0000) | (x<<24);
}

[[maybe_unused]] static void print_name(char* p, std::uint32_t n) {
    for(std::uint32_t j=0;j<n;j++) {
        if (p[j])
            std::cout << p[j];
    }
}

#if defined(__APPLE__)
static int read_macho(const args_t& args, std::uint8_t* f, [[maybe_unused]]void* end, uint_t size) {
    int verbose = 0;
    struct fat_header* hdr = reinterpret_cast<struct fat_header*>(f);
    symbols_t symbols;

    struct mach_header_64* mhdr = 0;
    [[maybe_unused]]std::uint32_t msz = 0;

    if (hdr->magic == FAT_CIGAM) { // fat header content is big-endian
        std::uint32_t narch = bswap(hdr->nfat_arch);
        if (verbose) std::cout << "narch: " << narch << '\n';
        for (std::uint32_t i=0; i<narch ; i++) {
            struct fat_arch* fa = reinterpret_cast<struct fat_arch*>(f+sizeof(struct fat_header)+ i*sizeof(struct fat_arch));
            if (static_cast<std::uint32_t>(fa->cputype) == bswap(CPU_TYPE_ARM64)) {
                if (verbose) {
                    std::cout << "Found ARM64 in fat binary: " << i
                              << " offset: " << bswap(fa->offset)
                              << " size: " << bswap(fa->size)
                              << '\n';
                }
                mhdr = reinterpret_cast<struct mach_header_64*>(f + bswap(fa->offset));
                msz = bswap(fa->size);
            }
        }
    }
    else {
        if (verbose)
            std::cout << "not an fat binary...\n";
        mhdr = reinterpret_cast<struct mach_header_64*>(f);
        msz = size;
    }

    if (verbose)
        std::cout << "mach magic " << std::hex << mhdr->magic << '\n';
    if (mhdr->magic != MH_MAGIC_64) {
        std::cout << "Not an macho64 binary\n";
        return -1;
    }
    if (mhdr->cputype != CPU_TYPE_ARM64) {
        std::cout << "Not an arm64 binary\n";
        return -1;
    }

    std::uint8_t* ft = reinterpret_cast<std::uint8_t*>(mhdr);  // start of arm64 part of file
    struct load_command* lc = reinterpret_cast<struct load_command*>(ft + sizeof(struct mach_header_64));
    std::uint8_t* lcu = reinterpret_cast<std::uint8_t*>(lc);
    //std::uint8_t* e = lcu + mhdr->sizeofcmds;
    std::uint32_t cmdsz = 0; // running total of the load_command structure sizes
    if (verbose)
        std::cout << "load commands: " << mhdr->ncmds << '\n';

    // read once to find symbols
    for(std::uint32_t i=0;i< mhdr->ncmds; i++)  {
        // if a load command is a segment, re-cast to segment and use that structure
        struct load_command* lci = reinterpret_cast<struct load_command*>(lcu + cmdsz); // add units of sizeof(struct load_command)
        cmdsz += lci->cmdsize;
        if (verbose) {
            std::cout << "LCI i: " << i
                      << " cmd: "  << std::hex  << lci->cmd << std::dec
                      << '\n';
        }
        if (lci->cmd == LC_SYMTAB) {
            struct symtab_command* syc = reinterpret_cast<struct symtab_command*>(lci);
            if (verbose)
                std::cout << i  << " LC_SYMTAB, sz= " << lci->cmdsize << '\n';

            for(std::uint32_t j=0; j< syc->nsyms;j++) {
                struct nlist_64* ns = reinterpret_cast<struct nlist_64*>(ft+syc->symoff) + j;
                if (verbose)
                    std::cout << i << ": "
                              << " indx: " << (std::uint32_t)(ns->n_un.n_strx)
                              << " type: " << (std::uint32_t)(ns->n_type)
                              << " nsect: " << (std::uint32_t)(ns->n_sect)
                              << " desc: " << ns->n_desc
                              << " val: " << std::hex << ns->n_value << std::dec  // symbol location
                              << '\n';

                if (ns->n_un.n_strx)  {
                    char* cp = reinterpret_cast<char*>(ft + syc->stroff) + ns->n_un.n_strx;
                    char* str_lim = reinterpret_cast<char*>(ft + syc->stroff + syc->strsize);
                    if (verbose)
                        print_char_until_null(cp,str_lim,1000);
                    symbols[ ns->n_value ] = std::string(cp);
                }
            }
        }
        else if (lci->cmd == LC_DYSYMTAB) {
            struct dysymtab_command* syc = reinterpret_cast<struct dysymtab_command*>(lci);
            if (verbose)
                std::cout << i  << " LC_DYSYMTAB, sz= " << lci->cmdsize << '\n';
            //FIXME...
            (void)syc;
        }
    }

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

    // read again to disassemble with symbols
    cmdsz = 0; // running total of the load_command structure sizes
    for(std::uint32_t i=0;i< mhdr->ncmds; i++)  {
        // if a load command is a segment, re-cast to segment and use that structure
        struct load_command* lci = reinterpret_cast<struct load_command*>(lcu + cmdsz); // add units of sizeof(struct load_command)
        cmdsz += lci->cmdsize;
        if (lci->cmd == LC_SEGMENT_64) {
            if (verbose)
                std::cout << i  << " LC_SEGMENT_64, sz= " << lci->cmdsize << '\n';
            struct segment_command_64* sc = reinterpret_cast<segment_command_64*>(lci);
            if (verbose)  {
                std::cout << "segment name: [";
                print_name(sc->segname,16);
                std::cout << "]\n";
                std::cout << "va:" << std::hex << sc->vmaddr
                          << " vsz: " << std::dec << sc->vmsize
                          << " offset: " << sc->fileoff
                          << " fsz: " << sc->filesize
                          << " nsect: " << sc->nsects
                          << '\n';
            }

            // sections follow each segment
            std::uint8_t* tp = reinterpret_cast<std::uint8_t*>(lci) + sizeof(struct segment_command_64);
            for(std::uint32_t j=0 ; j< sc->nsects; j++ ) {
                struct section_64* sh = reinterpret_cast<struct section_64*>(tp + j*sizeof(struct section_64));
                if (sc->filesize) {
                    if (verbose) {
                        std::cout << j << ": section name: [";
                        print_name(sh->sectname,16);
                        std::cout << "] -- segname: [";
                        print_name(sh->segname,16);
                        std::cout << "]\n";
                        if ((sh->flags & SECTION_TYPE) == S_REGULAR) {
                            std::cout << "regular\n";
                        }
                        if (sh->flags & S_ATTR_PURE_INSTRUCTIONS) {
                            std::cout << "instr\n";
                        }
                        std::cout << "addr: " << std::hex << sh->addr << std::dec
                                  << " size: " << sh->size
                                  << " offset: " << sh->offset
                                  << '\n';
                    }

                    if (sh->flags & S_ATTR_PURE_INSTRUCTIONS) {
                        // subtle point: basing offset off of ft
                        // (arm64 part), not f (file start) for fat
                        // binaries.
                        std::uint8_t* itext = ft + sh->offset;
                        std::uint32_t size = sh->size;
                        //dump(itext, size, sh->addr);
                        dump_instr(args, symbols, itext, size, sh->addr);
                    }
                }
            }
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
    int status [[maybe_unused]] = fstat(fd, &s);
    assert(status == 0);

    uint_t  size = s.st_size;
    std::uint8_t* f = static_cast<std::uint8_t*>(mmap(0, size, PROT_READ, MAP_PRIVATE, fd, 0));
    void* end = f + size;
    assert(f != 0);
#if defined(__APPLE__) && defined(__arm64__)
    int r = read_macho(args, f,end,size);
#else
    std::cout << "This only runs on Apple ARM64 chips\n";
    int r = -1;
    (void)f; (void)end; (void)size;
#endif
    return r;
}
