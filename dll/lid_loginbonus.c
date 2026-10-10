/*
 * lid-loginbonus.dll for Let It Die - a plugin for the LiD Plugin Loader that
 * brings back a daily login bonus, picked at random. It changes game code in
 * memory only.
 *
 * The online game's server decided each day's login bonus from a calendar
 * (master_login_bonus). The offline game never reads that table, but the rest
 * survived:
 *
 *   - When you reach the hub, the game's built-in server runs a once-a-day
 *     check. It records the new game day (10:00 UTC) and answers yes exactly
 *     once per day. The same yes restocks Hernia's vending machine.
 *   - The function the server used to turn a reward id (master_reward) into a
 *     present in the Rewards Box is still in the game.
 *
 * The plugin hooks the daily check. When the game's own answer is yes, it
 * picks a reward at random, every reward with the same chance, from the
 * rewards the online calendar gave at the player's floor tier, and hands it to
 * that function, which builds the present and saves it like any other. With
 * the Express Pass active, a second reward is picked from the same list.
 *
 * Only rewards that arrive as one present are used. The game makes one
 * present per item, mushroom, beast, decal, part or Lost Bag, so "4 rare
 * metals" would be 4 presents; amounts (Kill Coins, SPLithium, Death Metals,
 * Bloodnium, Express Pass days) are always one.
 *
 *   Floor tier    master_login_bonus rows    decided by
 *   1-20F         DATE                       (always)
 *   21-40F        DATE_O21                   the 21F or 31F elevator stop is open
 *   41F and up    DATE_O41                   the 41F or 51F elevator stop is open
 *
 * A mod can give a tier its own list instead: rows with cond_type
 * LID_RANDOM, LID_RANDOM_O21 or LID_RANDOM_O41, and cond_days as the weight
 * (a row with weight 3 comes up three times as often as one with weight 1).
 * When a tier has such rows, only they are used.
 *
 * The picks are random but fixed per login day: they come from a number made
 * once for this install (lid-loginbonus.seed) and the save's login count,
 * which the daily check raises by one each new day. Quitting without saving
 * and coming back gives the same rewards, not a new roll; every day the game
 * counts, including one started early with the mod manager's "Start a new
 * game day", gives a new roll.
 *
 * Which game functions to use, and the idea of hooking the daily check, come
 * from LetItRevive by Claudia-diva (MIT licence), which hands out the old
 * calendar's rewards date by date.
 *
 * Safety rules:
 *   - Each piece is found by its exact bytes, and must be found exactly once
 *     (the game's two "open the hub stop" calls must name the same player
 *     data, and the login count must sit where the days-in-a-row and the
 *     last login say), or nothing is changed.
 *   - Changed in the game: the first 18 bytes of the daily check.
 *   - masters.db is opened read-only. No network, no registry. The files it
 *     writes are next to itself: lid-loginbonus.log, lid-loginbonus.seed, and
 *     lid-loginbonus.busy while a reward is being handed over. If the game
 *     closes while that file is there, the plugin stays off until it is
 *     deleted, so a crash can never repeat every time the game starts.
 *   - Delete lid-loginbonus.dll and the game is exactly stock again.
 */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "sqlite3.h"
#include "version.h"

/* ------------------------------------------------------------------ log */

static wchar_t g_dir[MAX_PATH];        /* the DLL's folder, with a trailing backslash */
static wchar_t g_log_path[MAX_PATH];
static int g_log_started;

static void log_line(const char *fmt, ...)
{
    char msg[512];
    SYSTEMTIME t;
    va_list ap;
    int n;
    HANDLE f;
    DWORD written;

    if (!g_log_path[0])
        return;
    GetLocalTime(&t);
    n = snprintf(msg, sizeof msg, "%02u:%02u:%02u  ", t.wHour, t.wMinute, t.wSecond);
    va_start(ap, fmt);
    n += vsnprintf(msg + n, sizeof msg - n - 2, fmt, ap);
    va_end(ap);
    if (n > (int)sizeof msg - 3)
        n = (int)sizeof msg - 3;
    msg[n++] = '\r';
    msg[n++] = '\n';

    /* a fresh log each time the game starts */
    f = CreateFileW(g_log_path, g_log_started ? FILE_APPEND_DATA : GENERIC_WRITE,
                    FILE_SHARE_READ, NULL, g_log_started ? OPEN_ALWAYS : CREATE_ALWAYS,
                    FILE_ATTRIBUTE_NORMAL, NULL);
    if (f == INVALID_HANDLE_VALUE)
        return;
    g_log_started = 1;
    WriteFile(f, msg, (DWORD)n, &written, NULL);
    CloseHandle(f);
}

/* a file next to the DLL */
static int beside_me(const wchar_t *name, wchar_t *out)
{
    if (!g_dir[0] || wcslen(g_dir) + wcslen(name) >= MAX_PATH)
        return 0;
    wcscpy(out, g_dir);
    wcscat(out, name);
    return 1;
}

/* ------------------------------------------------------------ helpers */

struct span {
    unsigned char *start;
    size_t size;
};

static int section(unsigned char *base, const char *name, struct span *out)
{
    IMAGE_DOS_HEADER *dos = (IMAGE_DOS_HEADER *)base;
    IMAGE_NT_HEADERS64 *nt;
    IMAGE_SECTION_HEADER *sec;
    WORD i;

    if (dos->e_magic != IMAGE_DOS_SIGNATURE)
        return 0;
    nt = (IMAGE_NT_HEADERS64 *)(base + dos->e_lfanew);
    if (nt->Signature != IMAGE_NT_SIGNATURE)
        return 0;
    sec = IMAGE_FIRST_SECTION(nt);
    for (i = 0; i < nt->FileHeader.NumberOfSections; i++, sec++) {
        if (strncmp((const char *)sec->Name, name, IMAGE_SIZEOF_SHORT_NAME) == 0) {
            out->start = base + sec->VirtualAddress;
            out->size = sec->Misc.VirtualSize;
            return 1;
        }
    }
    return 0;
}

static size_t image_size(unsigned char *base)
{
    IMAGE_DOS_HEADER *dos = (IMAGE_DOS_HEADER *)base;
    IMAGE_NT_HEADERS64 *nt = (IMAGE_NT_HEADERS64 *)(base + dos->e_lfanew);
    return nt->OptionalHeader.SizeOfImage;
}

static int inside(struct span s, const void *p)
{
    return (const unsigned char *)p >= s.start && (const unsigned char *)p < s.start + s.size;
}

/* pattern with a mask: 'x' = must match, '?' = any byte */
static int matches(const unsigned char *p, const unsigned char *pat, const char *mask)
{
    size_t i;
    for (i = 0; mask[i]; i++)
        if (mask[i] == 'x' && p[i] != pat[i])
            return 0;
    return 1;
}

static int find_all(struct span s, const unsigned char *pat, const char *mask, unsigned char **last)
{
    size_t i, n = strlen(mask);
    int hits = 0;
    for (i = 0; i + n <= s.size; i++) {
        if (s.start[i] == pat[0] && matches(s.start + i, pat, mask)) {
            hits++;
            *last = s.start + i;
        }
    }
    return hits;
}

static unsigned char *call_target(unsigned char *call)
{
    int32_t r;
    memcpy(&r, call + 1, 4);
    return call + 5 + r;
}

/* where a rip-relative field points (the instruction ends right after it) */
static unsigned char *rip_target(unsigned char *field)
{
    int32_t r;
    memcpy(&r, field, 4);
    return field + 4 + r;
}

static int write_code(unsigned char *at, const void *bytes, size_t n)
{
    DWORD old;
    if (!VirtualProtect(at, n, PAGE_EXECUTE_READWRITE, &old))
        return 0;
    memcpy(at, bytes, n);
    VirtualProtect(at, n, old, &old);
    FlushInstructionCache(GetCurrentProcess(), at, n);
    return 1;
}

/* is this exact wide string (with its ending 0) at p? */
static int wide_at(struct span s, const unsigned char *p, const wchar_t *text)
{
    size_t n = (wcslen(text) + 1) * sizeof(wchar_t);
    return inside(s, p) && p + n <= s.start + s.size && memcmp(p, text, n) == 0;
}

/* --------------------------------------------------------- the pieces */

/* The daily check: sub rsp,0x28 / lea rcx,[rsp+0x30] / or rdx,-1 / call /
 * mov rcx,[rax] / call / mov rcx,rax / call (already counted today?) /
 * test al,al / je / xor al,al / add rsp,0x28 / ret / call now /
 * xor ecx,ecx / mov dl,1 / cmp [Express Pass end],rax ... */
static const unsigned char DAILY_START[] = {
    0x48, 0x83, 0xEC, 0x28, 0x48, 0x8D, 0x4C, 0x24, 0x30, 0x48, 0x83, 0xCA, 0xFF,
    0xE8, 0, 0, 0, 0, 0x48, 0x8B, 0x08, 0xE8, 0, 0, 0, 0, 0x48, 0x8B, 0xC8, 0xE8, 0, 0, 0, 0,
    0x84, 0xC0, 0x74, 0x07, 0x32, 0xC0, 0x48, 0x83, 0xC4, 0x28, 0xC3,
    0xE8, 0, 0, 0, 0, 0x33, 0xC9, 0xB2, 0x01, 0x48, 0x39, 0x05, 0, 0, 0, 0,
};
static const char DAILY_MASK[] = "xxxxxxxxxxxxxx????xxxx????xxxx????xxxxxxxxxxxx????xxxxxxx????";
#define DAILY_HOOK_SIZE 18   /* three whole instructions and the first call */
#define DAILY_CALL_AT   13
#define DAILY_NOW_AT    45
#define DAILY_VIP_AT    57   /* the rip-relative field of cmp [end],rax */

/* Turns a reward id into presents: mov rax,rsp / push rbp,r12,r13,r14,r15 /
 * lea rbp,[rax-0x48] / sub rsp,0x120 / mov qword [rbp+0x10],-2 /
 * save rbx,rsi,rdi / mov r12,r8 / mov rbx,rdx / mov r13,rcx. Many functions
 * could start like this; the one that also logs "rwdId not found!" within its
 * first 0x80 bytes is the one. */
static const unsigned char GRANT_START[] = {
    0x48, 0x8B, 0xC4, 0x55, 0x41, 0x54, 0x41, 0x55, 0x41, 0x56, 0x41, 0x57, 0x48, 0x8D, 0x68, 0xB8,
    0x48, 0x81, 0xEC, 0x20, 0x01, 0x00, 0x00, 0x48, 0xC7, 0x45, 0x10, 0xFE, 0xFF, 0xFF, 0xFF,
    0x48, 0x89, 0x58, 0x08, 0x48, 0x89, 0x70, 0x10, 0x48, 0x89, 0x78, 0x18,
    0x4D, 0x8B, 0xE0, 0x48, 0x8B, 0xDA, 0x4C, 0x8B, 0xE9,
};
static const char GRANT_MASK[] = "xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx";
static const wchar_t GRANT_MESSAGE[] = L"rwdId not found! rwdId:%s";
#define GRANT_MESSAGE_WITHIN 0x80

/* Opens an elevator stop for the player: push rdi / sub rsp,0x40 /
 * mov qword [rsp+0x20],-2 / mov [rsp+0x50],rbx / mov rbx,rdx /
 * lea rdi,[rcx+OPEN_FLOORS] / mov rax,[rdi] / mov rcx,rdi / call [rax+FIND] /
 * test rax,rax / je / xor al,al (already open). OPEN_FLOORS is where the
 * player data keeps the open stops, FIND the slot of "find this one". */
static const unsigned char OPEN_START[] = {
    0x40, 0x57, 0x48, 0x83, 0xEC, 0x40, 0x48, 0xC7, 0x44, 0x24, 0x20, 0xFE, 0xFF, 0xFF, 0xFF,
    0x48, 0x89, 0x5C, 0x24, 0x50, 0x48, 0x8B, 0xDA, 0x48, 0x8D, 0xB9, 0, 0, 0, 0,
    0x48, 0x8B, 0x07, 0x48, 0x8B, 0xCF, 0xFF, 0x50, 0, 0x48, 0x85, 0xC0, 0x74, 0x07, 0x32, 0xC0,
};
static const char OPEN_MASK[] = "xxxxxxxxxxxxxxxxxxxxxxxxxx????xxxxxxxx?xxxxxxx";
#define OPEN_FLOORS_AT 26
#define OPEN_FIND_AT   38

/* Records a new login day: push rbx / sub rsp,0x20 / inc [login count] /
 * mov rbx,rcx / lea rdx,[rcx-86400] / lea rcx,[rsp+0x30] / call / mov rcx,[rax] /
 * call / mov rcx,[last login] / cmp rax,rcx / jne / inc [days in a row]. The
 * last login sits 8 bytes after the count and the days in a row 4 after it,
 * which the plugin checks. */
static const unsigned char LOGIN_START[] = {
    0x40, 0x53, 0x48, 0x83, 0xEC, 0x20, 0xFF, 0x05, 0, 0, 0, 0, 0x48, 0x8B, 0xD9,
    0x48, 0x8D, 0x91, 0x80, 0xAE, 0xFE, 0xFF, 0x48, 0x8D, 0x4C, 0x24, 0x30, 0xE8, 0, 0, 0, 0,
    0x48, 0x8B, 0x08, 0xE8, 0, 0, 0, 0, 0x48, 0x8B, 0x0D, 0, 0, 0, 0, 0x48, 0x3B, 0xC1, 0x75, 0x08,
    0xFF, 0x05, 0, 0, 0, 0,
};
static const char LOGIN_MASK[] = "xxxxxxxx????xxxxxxxxxxxxxxxx????xxxx????xxx????xxxxxxx????";
#define LOGIN_COUNT_AT 8
#define LOGIN_LAST_AT  43
#define LOGIN_ROW_AT   54

/* The game opening the hub stop: lea rdx,[L"ELV_MAIN_HUB"] / lea rcx,[player] /
 * call open. Names the player data. */
static const unsigned char HUB_CALL[] = {0x48, 0x8D, 0x15, 0, 0, 0, 0, 0x48, 0x8D, 0x0D, 0, 0, 0, 0,
                                         0xE8, 0, 0, 0, 0};
static const char HUB_MASK[] = "xxx????xxx????x????";

struct pieces {
    unsigned char *daily, *daily_callee, *now, *vip_end;
    unsigned char *grant;
    int grants;                 /* functions that look like the granter */
    unsigned char *open;
    int32_t open_floors;
    int open_find;
    unsigned char *player;
    int hub_calls, hub_mismatch;
    unsigned char *login_count;
};

/* Find everything, without changing anything. `base` is the game's exe in
 * memory (the tests pass a mapped copy of it instead). */
static void find_pieces(unsigned char *base, struct pieces *pc)
{
    struct span text, rdata;
    unsigned char *p;
    size_t i;

    memset(pc, 0, sizeof *pc);
    if (!section(base, ".text", &text) || !section(base, ".rdata", &rdata))
        return;

    if (find_all(text, DAILY_START, DAILY_MASK, &pc->daily) == 1) {
        pc->daily_callee = call_target(pc->daily + DAILY_CALL_AT);
        pc->now = call_target(pc->daily + DAILY_NOW_AT);
        pc->vip_end = rip_target(pc->daily + DAILY_VIP_AT);
        if (!inside(text, pc->daily_callee) || !inside(text, pc->now))
            pc->daily = NULL;
    } else {
        pc->daily = NULL;
    }

    for (i = 0; i + sizeof GRANT_START + GRANT_MESSAGE_WITHIN <= text.size; i++) {
        size_t j;
        p = text.start + i;
        if (p[0] != 0x48 || !matches(p, GRANT_START, GRANT_MASK))
            continue;
        for (j = 0; j < GRANT_MESSAGE_WITHIN; j++) {
            if (p[j] == 0x48 && p[j + 1] == 0x8D && p[j + 2] == 0x15 &&
                wide_at(rdata, rip_target(p + j + 3), GRANT_MESSAGE)) {
                pc->grant = p;
                pc->grants++;
                break;
            }
        }
    }
    if (pc->grants != 1)
        pc->grant = NULL;

    if (find_all(text, LOGIN_START, LOGIN_MASK, &p) == 1) {
        unsigned char *count = rip_target(p + LOGIN_COUNT_AT);
        if (rip_target(p + LOGIN_LAST_AT) == count + 8 && rip_target(p + LOGIN_ROW_AT) == count + 4)
            pc->login_count = count;
    }

    if (find_all(text, OPEN_START, OPEN_MASK, &pc->open) == 1) {
        memcpy(&pc->open_floors, pc->open + OPEN_FLOORS_AT, 4);
        pc->open_find = pc->open[OPEN_FIND_AT];
        if (pc->open_floors < 8 || pc->open_floors > 0x10000 || pc->open_find % 8)
            pc->open = NULL;
    } else {
        pc->open = NULL;
    }
    if (!pc->open)
        return;
    for (i = 0; i + sizeof HUB_CALL <= text.size; i++) {
        unsigned char *player;
        p = text.start + i;
        if (p[0] != 0x48 || !matches(p, HUB_CALL, HUB_MASK) || call_target(p + 14) != pc->open ||
            !wide_at(rdata, rip_target(p + 3), L"ELV_MAIN_HUB"))
            continue;
        player = rip_target(p + 10);
        if (pc->player && pc->player != player)
            pc->hub_mismatch++;
        pc->player = player;
        pc->hub_calls++;
    }
    if (pc->hub_calls < 1 || pc->hub_mismatch)
        pc->player = NULL;
}

/* ---------------------------------------------------- the game, live */

typedef unsigned char (*daily_fn)(void);
typedef long long (*now_fn)(void);
typedef unsigned char (*grant_fn)(const wchar_t *from, const wchar_t *rwdid, const wchar_t *fromval);
typedef void *(*find_fn)(void *open_floors, const wchar_t *id);

static struct pieces g_pc;
static unsigned char *g_base;
static daily_fn g_daily_original;   /* the trampoline: the displaced bytes, then back */

/* Is this elevator stop open? 1 yes, 0 no, -1 the player data does not look
 * as expected (then it is not touched). */
static int floor_open(const wchar_t *id)
{
    struct span text;
    unsigned char *floors = g_pc.player + g_pc.open_floors;
    void **vt = *(void ***)floors;
    size_t size = image_size(g_base);
    find_fn find;

    if (!section(g_base, ".text", &text) || (unsigned char *)vt < g_base ||
        (unsigned char *)vt >= g_base + size)
        return -1;
    find = (find_fn)vt[g_pc.open_find / 8];
    if (!inside(text, (void *)find))
        return -1;
    return find(floors, id) != NULL;
}

/* 0 = 1-20F, 1 = 21-40F, 2 = 41F and up */
static int floor_tier(void)
{
    static const wchar_t *const stops[2][2] = {
        {L"ELV_MAIN_AMS_FLR_01", L"ELV_MAIN_RFT_FLR_01"},   /* 21F, 31F */
        {L"ELV_MAIN_HZM_FLR_01", L"ELV_MAIN_HVN_FLR_01"},   /* 41F, 51F */
    };
    int tier, k, open;

    for (tier = 2; tier >= 1; tier--) {
        for (k = 0; k < 2; k++) {
            open = floor_open(stops[tier - 1][k]);
            if (open < 0) {
                log_line("  the player's elevator stops could not be read - using the 1-20F list");
                return 0;
            }
            if (open)
                return tier;
        }
    }
    return 0;
}

/* --------------------------------------------------------- the rewards */

#define MAX_POOL 4096
#define ID_SIZE  64

struct pool {
    char id[MAX_POOL][ID_SIZE];
    unsigned weight[MAX_POOL];
    int n;
    unsigned long long total;
};
static struct pool g_pool;

static int masters_path(char *out, int cap)
{
    wchar_t exe[MAX_PATH], path[MAX_PATH], *slash;
    DWORD n = GetModuleFileNameW(NULL, exe, MAX_PATH);

    if (n == 0 || n >= MAX_PATH)
        return 0;
    slash = wcsrchr(exe, L'\\');
    if (!slash || (size_t)(slash - exe) + 40 >= MAX_PATH)
        return 0;
    wcscpy(slash + 1, L"..\\..\\BrgGame\\Content\\masters.db");   /* exe is in Binaries\Win64 */
    if (!GetFullPathNameW(exe, MAX_PATH, path, NULL))
        return 0;
    return WideCharToMultiByte(CP_UTF8, 0, path, -1, out, cap, NULL, NULL) > 0;
}

/* Read one list into g_pool; returns how many rewards it has. */
static int read_list(sqlite3 *db, const char *sql, const char *cond_type)
{
    sqlite3_stmt *st = NULL;

    g_pool.n = 0;
    g_pool.total = 0;
    if (sqlite3_prepare_v2(db, sql, -1, &st, NULL) != SQLITE_OK)
        return 0;
    sqlite3_bind_text(st, 1, cond_type, -1, SQLITE_STATIC);
    while (sqlite3_step(st) == SQLITE_ROW && g_pool.n < MAX_POOL) {
        const char *id = (const char *)sqlite3_column_text(st, 0);
        int weight = sqlite3_column_int(st, 1);
        if (!id || !*id || strlen(id) >= ID_SIZE)
            continue;
        strcpy(g_pool.id[g_pool.n], id);
        g_pool.weight[g_pool.n] = weight > 0 ? (unsigned)weight : 1;
        g_pool.total += g_pool.weight[g_pool.n];
        g_pool.n++;
    }
    sqlite3_finalize(st);
    return g_pool.n;
}

/* The tier's list: a mod's own (LID_RANDOM...) if there is one, else every
 * reward the online calendar gave at that tier that arrives as one present,
 * each once - several ids that give the very same thing (the calendar had
 * four for 20,000 Kill Coins) count as one, so it comes up no more often than
 * the rest. Only rewards that are in master_reward, so the game knows them
 * all. */
static const char *load_pool(int tier)
{
    static const char *const own[3] = {"LID_RANDOM", "LID_RANDOM_O21", "LID_RANDOM_O41"};
    static const char *const online[3] = {"DATE", "DATE_O21", "DATE_O41"};
    char path[MAX_PATH * 3];
    sqlite3 *db = NULL;
    const char *used = NULL;

    g_pool.n = 0;
    if (!masters_path(path, sizeof path)) {
        log_line("  could not work out where masters.db is");
        return NULL;
    }
    if (sqlite3_open_v2(path, &db, SQLITE_OPEN_READONLY, NULL) != SQLITE_OK) {
        log_line("  could not open %s: %s", path, db ? sqlite3_errmsg(db) : "out of memory");
        sqlite3_close(db);
        return NULL;
    }
    sqlite3_busy_timeout(db, 2000);
    if (read_list(db, "SELECT rwdid, cond_days FROM master_login_bonus WHERE cond_type = ?1 "
                      "AND rwdid IN (SELECT rwdid FROM master_reward) ORDER BY rowid", own[tier]))
        used = own[tier];
    else if (read_list(db, "SELECT MIN(rwdid), 1 FROM master_reward WHERE rwdid IN "
                           "(SELECT rwdid FROM master_login_bonus WHERE cond_type = ?1) "
                           "AND rwdid IN (SELECT rwdid FROM master_reward GROUP BY rwdid HAVING COUNT(*) = 1 "
                           "AND (MAX(num) <= 1 OR MAX(type) IN ('MONEY', 'PAIDMONEY', 'SPIRIT', 'BLOODNIUM', "
                           "'MEDAL', 'PAIDMEDAL', 'VIP', '1VIP'))) "
                           "GROUP BY type, num, val0 ORDER BY 1", online[tier]))
        used = online[tier];
    else
        log_line("  masters.db has no %s or %s rewards: %s", own[tier], online[tier], sqlite3_errmsg(db));
    sqlite3_close(db);
    return used;
}

static uint64_t mix(uint64_t x)   /* splitmix64 */
{
    x += 0x9E3779B97F4A7C15ULL;
    x = (x ^ (x >> 30)) * 0xBF58476D1CE4E5B9ULL;
    x = (x ^ (x >> 27)) * 0x94D049BB133111EBULL;
    return x ^ (x >> 31);
}

/* This install's own number: read from lid-loginbonus.seed, made the first
 * time. With it the picks differ between players but not between two tries
 * at the same day. */
static uint64_t install_seed(void)
{
    wchar_t path[MAX_PATH];
    char text[32] = {0};
    uint64_t seed = 0;
    HANDLE f;
    DWORD got = 0;

    if (!beside_me(L"lid-loginbonus.seed", path))
        return 0x4C6F67696E426F6EULL;
    f = CreateFileW(path, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    if (f != INVALID_HANDLE_VALUE) {
        ReadFile(f, text, 16, &got, NULL);
        CloseHandle(f);
        if (got == 16 && sscanf(text, "%16llx", (unsigned long long *)&seed) == 1 && seed)
            return seed;
    }
    {
        LARGE_INTEGER qpc;
        FILETIME ft;
        QueryPerformanceCounter(&qpc);
        GetSystemTimeAsFileTime(&ft);
        seed = mix((uint64_t)qpc.QuadPart ^ ((uint64_t)ft.dwHighDateTime << 32 | ft.dwLowDateTime) ^
                   ((uint64_t)GetCurrentProcessId() << 40) ^ GetTickCount64() ^ (uint64_t)(uintptr_t)&qpc);
        if (!seed)
            seed = 1;
    }
    snprintf(text, sizeof text, "%016llx", (unsigned long long)seed);
    f = CreateFileW(path, GENERIC_WRITE, 0, NULL, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, NULL);
    if (f != INVALID_HANDLE_VALUE) {
        WriteFile(f, text, 16, &got, NULL);
        CloseHandle(f);
    } else {
        log_line("  could not save lid-loginbonus.seed - picks may change if you restart today");
    }
    return seed;
}

/* Pick one reward for this login day and slot (0 the daily one, 1 the
 * Express Pass one). Every reward's chance is its weight out of the list's
 * total. */
static const char *pick(uint64_t seed, int login, int slot)
{
    uint64_t roll = mix(seed ^ mix((uint64_t)(uint32_t)login) ^ ((uint64_t)slot << 56)) % g_pool.total;
    int i;
    for (i = 0; i < g_pool.n; i++) {
        if (roll < g_pool.weight[i])
            return g_pool.id[i];
        roll -= g_pool.weight[i];
    }
    return g_pool.id[g_pool.n - 1];
}

static int hand_over(const char *rwdid, const char *why)
{
    wchar_t wide[ID_SIZE];
    int i;
    unsigned char ok;

    for (i = 0; rwdid[i] && i < ID_SIZE - 1; i++)
        wide[i] = (wchar_t)(unsigned char)rwdid[i];
    wide[i] = 0;
    /* the game's own callers pass no sender text */
    ok = ((grant_fn)g_pc.grant)(L"LOGIN", wide, NULL);
    log_line("  %s: %s %s", why, rwdid, ok ? "is in the Rewards Box" : "- the game refused it");
    return ok != 0;
}

static void grant_today(void)
{
    static const char *const tier_name[3] = {"1-20F", "21-40F", "41F and up"};
    long long now = ((now_fn)g_pc.now)();
    int login = *(int *)g_pc.login_count;   /* already raised for today by the game */
    int vip, tier;
    const char *list;
    uint64_t seed;

    vip = *(long long *)g_pc.vip_end >= now;   /* the daily check's own test */
    tier = floor_tier();
    list = load_pool(tier);
    if (!list) {
        log_line("new game day: no login bonus, the reward list could not be read");
        return;
    }
    seed = install_seed();
    log_line("new game day (login %d): %s list (%s, %d rewards)%s", login, list, tier_name[tier], g_pool.n,
             vip ? ", Express Pass active" : "");
    hand_over(pick(seed, login, 0), "login bonus");
    if (vip)
        hand_over(pick(seed, login, 1), "Express Pass bonus");
}

/* The hooked daily check: the game's own check first, always. Only on its
 * yes - a new game day, recorded - is anything handed over, so the plugin
 * keeps no record of its own and can never give a day twice. */
static unsigned char on_daily_check(void)
{
    unsigned char fresh = g_daily_original();
    wchar_t busy[MAX_PATH];
    HANDLE f = INVALID_HANDLE_VALUE;

    if (!fresh)
        return fresh;
    busy[0] = 0;
    if (beside_me(L"lid-loginbonus.busy", busy))
        f = CreateFileW(busy, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (f != INVALID_HANDLE_VALUE) {
        static const char note[] =
            "LiD Login Bonus was handing over a reward. If this file is still here,\r\n"
            "the game closed while it did, so the plugin stays off until you delete it.\r\n";
        DWORD written;
        WriteFile(f, note, sizeof note - 1, &written, NULL);
        CloseHandle(f);
    }
    grant_today();
    if (busy[0])
        DeleteFileW(busy);
    return fresh;
}

/* Send the daily check to on_daily_check. The trampoline runs the 18 bytes
 * it displaced (its call rebuilt as an absolute one) and jumps back. Both
 * jumps go through memory, so no register is touched. */
static int hook_daily(void)
{
    unsigned char *at = g_pc.daily, *tramp, patch[DAILY_HOOK_SIZE];
    void *hook = (void *)on_daily_check, *callee = g_pc.daily_callee, *resume = at + DAILY_HOOK_SIZE;

    tramp = VirtualAlloc(NULL, 64, MEM_RESERVE | MEM_COMMIT, PAGE_EXECUTE_READWRITE);
    if (!tramp)
        return 0;
    memcpy(tramp, at, DAILY_CALL_AT);                       /* sub / lea / or */
    tramp[13] = 0xFF; tramp[14] = 0x15;                     /* call [rip+6] */
    tramp[15] = 6; tramp[16] = tramp[17] = tramp[18] = 0;
    tramp[19] = 0xFF; tramp[20] = 0x25;                     /* jmp [rip+8] */
    tramp[21] = 8; tramp[22] = tramp[23] = tramp[24] = 0;
    memcpy(tramp + 25, &callee, 8);
    memcpy(tramp + 33, &resume, 8);
    FlushInstructionCache(GetCurrentProcess(), tramp, 64);
    g_daily_original = (daily_fn)tramp;

    patch[0] = 0xFF; patch[1] = 0x25;                       /* jmp [rip+0] */
    memset(patch + 2, 0, 4);
    memcpy(patch + 6, &hook, 8);
    memset(patch + 14, 0x90, DAILY_HOOK_SIZE - 14);
    if (!write_code(at, patch, sizeof patch)) {
        VirtualFree(tramp, 0, MEM_RELEASE);
        return 0;
    }
    return 1;
}

/* ------------------------------------------------------------ start */

static void start(void)
{
    wchar_t busy[MAX_PATH];

    if (beside_me(L"lid-loginbonus.busy", busy) && GetFileAttributesW(busy) != INVALID_FILE_ATTRIBUTES) {
        log_line("the game closed while a reward was being handed over last time, so the login bonus "
                 "stays off - nothing changed. Delete lid-loginbonus.busy to turn it back on.");
        return;
    }
    g_base = (unsigned char *)GetModuleHandleW(NULL);
    find_pieces(g_base, &g_pc);
    if (!g_pc.daily || !g_pc.grant || !g_pc.open || !g_pc.player || !g_pc.login_count) {
        log_line("the game functions were not found exactly once (daily check %s, reward granter %s "
                 "[%d like it], elevator stops %s, player data %s [%d calls, %d disagree], login count %s) "
                 "- nothing changed. A game update may have moved them.",
                 g_pc.daily ? "yes" : "NO", g_pc.grant ? "yes" : "NO", g_pc.grants,
                 g_pc.open ? "yes" : "NO", g_pc.player ? "yes" : "NO", g_pc.hub_calls, g_pc.hub_mismatch,
                 g_pc.login_count ? "yes" : "NO");
        return;
    }
    if (!hook_daily()) {
        log_line("could not hook the daily check - nothing changed");
        return;
    }
    log_line("ready: the daily check (game+0x%llX) hands out a random login bonus; rewards via "
             "game+0x%llX, player data game+0x%llX",
             (unsigned long long)(g_pc.daily - g_base), (unsigned long long)(g_pc.grant - g_base),
             (unsigned long long)(g_pc.player - g_base));
}

static int running_in_the_game(void)
{
    wchar_t exe[MAX_PATH];
    const wchar_t *name;
    DWORD n = GetModuleFileNameW(NULL, exe, MAX_PATH);

    if (n == 0 || n >= MAX_PATH)
        return 0;
    name = wcsrchr(exe, L'\\');
    name = name ? name + 1 : exe;
    return _wcsicmp(name, L"BrgGame-Steam.exe") == 0;
}

#ifndef LID_LOGINBONUS_NO_DLLMAIN
BOOL WINAPI DllMain(HINSTANCE inst, DWORD reason, LPVOID reserved)
{
    (void)reserved;
    if (reason == DLL_PROCESS_ATTACH) {
        wchar_t *slash;
        DWORD n;

        DisableThreadLibraryCalls(inst);
        n = GetModuleFileNameW(inst, g_dir, MAX_PATH);
        slash = (n && n < MAX_PATH - 32) ? wcsrchr(g_dir, L'\\') : NULL;
        if (slash) {
            slash[1] = 0;
            wcscpy(g_log_path, g_dir);
            wcscat(g_log_path, L"lid-loginbonus.log");
        } else {
            g_dir[0] = 0;
        }

        if (!running_in_the_game())
            return TRUE;    /* some other program - change nothing */
        log_line("LiD Login Bonus lid-loginbonus.dll " LID_DLL_VERSION " loaded");
        start();
    }
    return TRUE;
}
#endif
