
ADIS - Aarch64 Decoder/Disassembler

Organization
doc     - overview presentation
pysrc   - main python sources for reading machine-readable instruction data and generating C++ code
          genadis.py is the top level script run by CMakeLists.txt during the build.
          instr.py is where most of the instruction related support comes from
src     -  C++ support code referred to by generated code and the elf/coff/raw disassembler programs
data    - vendored AARCHMRS instruction data and its license notice
pyext   - python extension so one can call the shared object from python


Building using nix:

- mkdir build
- cd build
- /bin/time nix-shell ../project.nix --run "cmake -G Ninja .. && ninja"
