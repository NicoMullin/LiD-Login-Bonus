/* The plugin's search, run on the real BrgGame-Steam.exe (mapped as a file,
 * nothing runs). Every piece must be found exactly once. Only run by
 * `build.py --exe PATH`: the game is not part of this repository. */
#define LID_LOGINBONUS_NO_DLLMAIN
#include "../dll/lid_loginbonus.c"

#include <stdio.h>

static unsigned long long rva(unsigned char *p, unsigned char *base)
{
    return p ? (unsigned long long)(p - base) : 0ULL;
}

int main(int argc, char **argv)
{
    struct pieces pc;
    unsigned char *base;
    HMODULE m;
    int fails = 0;

    if (argc < 2) { puts("usage: realexe PATH-TO-BrgGame-Steam.exe"); return 2; }
    m = LoadLibraryExA(argv[1], NULL, LOAD_LIBRARY_AS_IMAGE_RESOURCE | LOAD_LIBRARY_AS_DATAFILE);
    if (!m) { printf("could not map %s (error %lu)\n", argv[1], GetLastError()); return 2; }
    base = (unsigned char *)((ULONG_PTR)m & ~(ULONG_PTR)3);

    find_pieces(base, &pc);
    /* where they are in game build 5.0.4.4 (1.91) */
    printf("daily check            %-7s game+0x%llX (1.91: game+0x12DC530)\n", pc.daily ? "found" : "MISSING",
           rva(pc.daily, base));
    printf("  game clock           game+0x%llX (1.91: game+0xC457F0)\n", rva(pc.now, base));
    printf("  Express Pass end     game+0x%llX (1.91: game+0xF8A0198)\n", rva(pc.vip_end, base));
    printf("reward granter         %-7s game+0x%llX (1.91: game+0x131A9D0), %d like it (needs 1)\n",
           pc.grant ? "found" : "MISSING", rva(pc.grant, base), pc.grants);
    printf("open an elevator stop  %-7s game+0x%llX (1.91: game+0x1341CE0), stops at +0x%X, find in slot %d\n",
           pc.open ? "found" : "MISSING", rva(pc.open, base), (unsigned)pc.open_floors, pc.open_find / 8);
    printf("player data            %-7s game+0x%llX (1.91: game+0xF89FFD0), %d hub calls, %d disagree\n",
           pc.player ? "found" : "MISSING", rva(pc.player, base), pc.hub_calls, pc.hub_mismatch);
    printf("login count            %-7s game+0x%llX (1.91: game+0x27D428C)\n",
           pc.login_count ? "found" : "MISSING", rva(pc.login_count, base));
    if (!pc.daily || !pc.grant || !pc.open || !pc.player || !pc.login_count)
        fails++;
    printf(fails ? "\n%d FAILED\n" : "\nall found\n", fails);
    return fails != 0;
}
