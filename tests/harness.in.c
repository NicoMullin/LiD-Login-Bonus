/* Stand-in for BrgGame-Steam.exe: the daily check, the reward granter, the
 * "open an elevator stop" function and the player data, in the game's byte
 * shapes. Loads lid-loginbonus.dll from its own folder, runs "days" through
 * the daily check and checks what was handed out. masters.db is a small test
 * copy that build.py writes two folders up, where the game keeps it.
 *
 * Modes (first argument):
 *   (none)    the normal checks
 *   own-list  masters.db has a mod's own 41F+ list (LID_RANDOM_O41)
 *   busy      lid-loginbonus.busy is there before the plugin loads
 *   no-db     there is no masters.db */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>
#include <string.h>
#include <wchar.h>

#define USED __attribute__((used))

/* ---- what the game's functions read and write ---- */
USED const wchar_t g_grant_message[] = L"rwdId not found! rwdId:%s";
USED const wchar_t g_other_text[] = L"something else";
USED const wchar_t g_hub_name[] = L"ELV_MAIN_HUB";
USED long long g_vip_end;               /* the Express Pass's end time */
USED __attribute__((aligned(16))) unsigned char g_player[0x400];
USED struct { int count, in_a_row; long long last; } g_login = {70, 1, 0};   /* the save's login count */
#define OPEN_FLOORS 0x2C8

static void *g_cell = (void *)0x1234;
static unsigned char g_already;         /* 1 = today is already counted */
static long long g_now;
static int g_records, g_recorded_vip;
static const wchar_t *g_open[4];        /* the elevator stops the player has */
static int g_bad_find;

#define MAX_GRANTS 16
static int g_grants;
static wchar_t g_from[MAX_GRANTS][32], g_rwdid[MAX_GRANTS][64];
static int g_fromval_null[MAX_GRANTS];

USED void **fake_ptr(void) { return &g_cell; }
USED void *fake_pass(void *p) { return p; }
USED unsigned char fake_already(void *p) { return p == (void *)0x1234 ? g_already : 2; }
USED long long fake_now(void) { return g_now; }
USED void fake_record(unsigned char vip, unsigned char one)
{
    g_records++;
    g_recorded_vip = one == 1 ? vip : 99;
    g_login.count++;       /* as the game's day record does */
}
USED unsigned char record_grant(const wchar_t *from, const wchar_t *rwdid, const wchar_t *fromval)
{
    if (g_grants < MAX_GRANTS) {
        wcsncpy(g_from[g_grants], from ? from : L"(null)", 31);
        wcsncpy(g_rwdid[g_grants], rwdid ? rwdid : L"(null)", 63);
        g_fromval_null[g_grants] = fromval == NULL;
    }
    g_grants++;
    return 1;
}

/* the open stops' "find this one", slot 5 of their vtable */
static void *fake_find(void *floors, const wchar_t *id)
{
    int i;
    if (floors != g_player + OPEN_FLOORS)
        g_bad_find++;
    for (i = 0; i < 4 && g_open[i]; i++)
        if (wcscmp(g_open[i], id) == 0)
            return floors;
    return NULL;
}
static void *g_floor_vt[8] = {0, 0, 0, 0, 0, (void *)fake_find, 0, 0};

__asm__(
@ASM@);

extern unsigned char fake_daily(void);

/* ---- the checks ---- */

static int fails;
static void expect(const char *what, int got, int want)
{
    printf("%-62s got %d want %d  %s\n", what, got, want, got == want ? "ok" : "FAIL");
    if (got != want) fails++;
}

#define DAY 86400LL
#define BASE (20000 * DAY + 10 * 3600)   /* the start of a game day */

/* One hub visit on day `day`: returns the daily check's answer. */
static int visit(long long day, int vip, const wchar_t *a, const wchar_t *b)
{
    g_now = BASE + day * DAY + 5000;
    g_vip_end = vip ? g_now + 1000 : g_now - 1000;
    g_open[0] = a;
    g_open[1] = a ? b : NULL;
    g_open[2] = NULL;
    g_grants = 0;
    return fake_daily();
}

static int in_list(const wchar_t *id, const wchar_t *const *list)
{
    for (; *list; list++)
        if (wcscmp(id, *list) == 0)
            return 1;
    return 0;
}

static const wchar_t *const BASE_LIST[] = {L"RWD_A", L"RWD_B", L"RWD_C", L"RWD_D", L"RWD_KC", NULL};
static const wchar_t *const O21_LIST[] = {L"RWD_O21_A", L"RWD_O21_B", NULL};
static const wchar_t *const O41_LIST[] = {L"RWD_O41_A", L"RWD_O41_B", NULL};

static void read_log(char *log, size_t cap)
{
    FILE *f = fopen("lid-loginbonus.log", "rb");
    memset(log, 0, cap);
    if (f) { fread(log, 1, cap - 1, f); fclose(f); }
}

static int exists(const char *name)
{
    return GetFileAttributesA(name) != INVALID_FILE_ATTRIBUTES;
}

int main(int argc, char **argv)
{
    static char log[16384];
    const char *mode = argc > 1 ? argv[1] : "";
    wchar_t first[64];
    int i, answer, counts[5] = {0}, all_in = 1, gone = 0, pile = 0, again = 0, same = 1;

    *(void ***)(g_player + OPEN_FLOORS) = g_floor_vt;

    if (strcmp(mode, "busy") == 0) {
        FILE *f = fopen("lid-loginbonus.busy", "wb");
        if (f) fclose(f);
    }

    puts("-- before the plugin");
    g_already = 0;
    answer = visit(0, 0, NULL, NULL);
    expect("a new day: the check says yes", answer, 1);
    expect("  and records the day", g_records, 1);
    expect("  nothing is handed out", g_grants, 0);

    if (!LoadLibraryW(L"lid-loginbonus.dll")) { printf("LoadLibrary failed %lu\n", GetLastError()); return 2; }
    read_log(log, sizeof log);
    printf("%s", log);

    if (strcmp(mode, "busy") == 0) {
        puts("-- a crashed hand-over last time");
        expect("the log says it stays off", strstr(log, "stays off") != NULL, 1);
        expect("  and that nothing changed", strstr(log, "nothing changed") != NULL, 1);
        answer = visit(1, 1, NULL, NULL);
        expect("the daily check still works", answer, 1);
        expect("  nothing is handed out", g_grants, 0);
        expect("  the busy file is left for the player", exists("lid-loginbonus.busy"), 1);
        goto done;
    }
    expect("the log says it is ready", strstr(log, "ready:") != NULL, 1);

    puts("-- with the plugin");
    g_already = 1;
    g_records = 0;
    answer = visit(1, 0, NULL, NULL);
    expect("day already counted: the check says no", answer, 0);
    expect("  nothing is handed out", g_grants, 0);
    expect("  no day recorded", g_records, 0);

    g_already = 0;
    answer = visit(1, 0, NULL, NULL);
    expect("a new day: the check still says yes", answer, 1);
    expect("  the game records the day", g_records, 1);
    expect("  with no Express Pass", g_recorded_vip, 0);
    expect("  the plugin never asks for stops the player has not", g_bad_find, 0);

    if (strcmp(mode, "no-db") == 0) {
        expect("no masters.db: nothing is handed out", g_grants, 0);
        read_log(log, sizeof log);
        expect("  the log says the list could not be read", strstr(log, "could not be read") != NULL, 1);
        expect("  the busy file is gone again", exists("lid-loginbonus.busy"), 0);
        goto done;
    }

    expect("one reward is handed out", g_grants, 1);
    expect("  from LOGIN", wcscmp(g_from[0], L"LOGIN") == 0, 1);
    expect("  with no sender text", g_fromval_null[0], 1);
    expect("  from the 1-20F list", in_list(g_rwdid[0], BASE_LIST), 1);
    expect("  the busy file is gone again", exists("lid-loginbonus.busy"), 0);
    expect("  this install's seed is saved", exists("lid-loginbonus.seed"), 1);
    wcscpy(first, g_rwdid[0]);

    g_login.count--;       /* quit without saving: the count was never saved */
    answer = visit(1, 0, NULL, NULL);
    expect("the same day again (quit without saving): same reward",
           g_grants == 1 && wcscmp(g_rwdid[0], first) == 0, 1);
    {
        int differs = 0;
        for (i = 0; i < 20; i++) {
            visit(1, 0, NULL, NULL);    /* the same date, a new day counted each time */
            differs |= g_grants == 1 && wcscmp(g_rwdid[0], first) != 0;
        }
        expect("new days started early (same date): the reward changes", differs, 1);
    }

    answer = visit(2, 1, NULL, NULL);
    expect("Express Pass: the game records it", g_recorded_vip, 1);
    expect("  two rewards are handed out", g_grants, 2);
    expect("  both from the same list", in_list(g_rwdid[0], BASE_LIST) && in_list(g_rwdid[1], BASE_LIST), 1);

    answer = visit(3, 0, L"ELV_MAIN_AMS_FLR_01", NULL);
    expect("21F stop open: from the 21-40F list", g_grants == 1 && in_list(g_rwdid[0], O21_LIST), 1);
    answer = visit(3, 0, L"ELV_MAIN_RFT_FLR_01", NULL);
    expect("31F stop open: from the 21-40F list", g_grants == 1 && in_list(g_rwdid[0], O21_LIST), 1);
    answer = visit(4, 0, L"ELV_MAIN_AMS_FLR_01", L"ELV_MAIN_HZM_FLR_01");
    if (strcmp(mode, "own-list") == 0) {
        int mine = 1;
        expect("41F stop open, a mod's own 41F+ list: its reward", g_grants == 1 &&
               wcscmp(g_rwdid[0], L"RWD_MINE") == 0, 1);
        for (i = 0; i < 30; i++) {
            visit(100 + i, 0, L"ELV_MAIN_HVN_FLR_01", NULL);
            mine &= g_grants == 1 && wcscmp(g_rwdid[0], L"RWD_MINE") == 0;
        }
        expect("  every day", mine, 1);
        visit(5, 0, NULL, NULL);
        expect("  1-20F still uses the online list", g_grants == 1 && in_list(g_rwdid[0], BASE_LIST), 1);
        goto done;
    }
    expect("41F stop open: from the 41F+ list", g_grants == 1 && in_list(g_rwdid[0], O41_LIST), 1);
    answer = visit(4, 0, L"ELV_MAIN_HVN_FLR_01", NULL);
    expect("51F stop open: from the 41F+ list", g_grants == 1 && in_list(g_rwdid[0], O41_LIST), 1);

    for (i = 0; i < 400; i++) {
        int k;
        visit(1000 + i, 0, NULL, NULL);
        if (g_grants != 1 || !in_list(g_rwdid[0], BASE_LIST)) {
            all_in = 0;
            if (g_grants == 1 && wcscmp(g_rwdid[0], L"RWD_GONE") == 0) gone++;
            if (g_grants == 1 && wcscmp(g_rwdid[0], L"RWD_PILE") == 0) pile++;
            if (g_grants == 1 && wcscmp(g_rwdid[0], L"RWD_A_AGAIN") == 0) again++;
            continue;
        }
        for (k = 0; k < 5; k++)
            if (wcscmp(g_rwdid[0], BASE_LIST[k]) == 0) counts[k]++;
        if (i && wcscmp(g_rwdid[0], first) != 0) same = 0;
    }
    expect("400 days: every pick is from the list", all_in, 1);
    expect("  a reward the game does not know is never picked", gone, 0);
    expect("  a reward of 4 items (4 presents) is never picked", pile, 0);
    expect("  the same reward under a second id counts once", again, 0);
    expect("  the picks change from day to day", same, 0);
    printf("  RWD_A %d, RWD_B %d, RWD_C %d, RWD_D %d, RWD_KC %d (RWD_A is in the calendar twice)\n",
           counts[0], counts[1], counts[2], counts[3], counts[4]);
    expect("  each reward comes up about as often (50+ of 400)",
           counts[0] >= 50 && counts[1] >= 50 && counts[2] >= 50 && counts[3] >= 50 && counts[4] >= 50, 1);
    read_log(log, sizeof log);
    expect("the log names the reward", strstr(log, "login bonus: RWD_") != NULL, 1);

done:
    printf(fails ? "\n%d FAILED\n" : "\nall passed\n", fails);
    return fails != 0;
}
