    .text
    .globl mingw_sample_magic
    .def mingw_sample_magic; .scl 2; .type 32; .endef
mingw_sample_magic:
    movabsq $0x4d4f4e494f534153, %rax
    ret
