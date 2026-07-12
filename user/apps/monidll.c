typedef unsigned int uint32_t;

__attribute__((visibility("default")))
uint32_t monios_dll_abi_version(void)
{
    return 1;
}

__attribute__((visibility("default")))
const char *monios_dll_name(void)
{
    return "moniapi";
}

__attribute__((visibility("default")))
uint32_t monios_dll_add(uint32_t left, uint32_t right)
{
    return left + right;
}
