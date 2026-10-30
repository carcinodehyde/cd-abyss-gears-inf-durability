// AbyssDurability.asi - lifts the 100-use limit on Abyss Gear at runtime.
//
// Hooks the game's resource reader (the same function Pack Rat hooks) and,
// when the item table (iteminfo.staticinfobody) is read, sets max_endurance of
// every Item_*AbyssGear* record that has a limited durability to 0xFFFF, the
// value the game uses for items that never wear out. Nothing on disk changes.
//
// The table is recognised by content, not by path: each record is
//   u32 nameLen, char name[nameLen], u32 key, u8 0, u16 max_endurance, ...
//
// Build: zig cc -target x86_64-windows-gnu -O2 -shared -o AbyssDurability.asi abyss_durability.c

#include <windows.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

typedef struct {
    uint8_t *data;   // +0x0
    uint32_t unk8;   // +0x8
    uint32_t size;   // +0xC
} ReadBuf;

typedef uint8_t (*ReadFn)(void *, void *, ReadBuf *, void *, uint64_t, uint64_t, uint64_t);

static ReadFn g_orig;
static char g_log_path[MAX_PATH];
static char g_ini_path[MAX_PATH];
static int g_enabled = 1;
static volatile LONG g_patched_tables;

// Signature starts 0x10 bytes into the function, past the prologue, so it still
// matches after another plugin (e.g. Pack Rat) has put a 5- or 14-byte jump there.
static const int SIG_OFFSET = 0x10;
static const char *SIG =
    "44 8B 8C 24 80 00 00 00 4C 8B C2 48 8D 54 24 30 "
    "E8 ?? ?? ?? ?? 90 48 8B 54 24 30 48 85 D2 74 2E 48 8B 4A 20 48 85 C9 74 25 48 8B 01 4C 8B 50 20";
static const uint8_t PROLOGUE[5] = {0x48, 0x89, 0x5C, 0x24, 0x08};

static void Log(const char *fmt, ...) {
    FILE *f = fopen(g_log_path, "a");
    if (!f) return;
    SYSTEMTIME t;
    GetLocalTime(&t);
    fprintf(f, "[%02d:%02d:%02d] ", t.wHour, t.wMinute, t.wSecond);
    va_list ap;
    va_start(ap, fmt);
    vfprintf(f, fmt, ap);
    va_end(ap);
    fputc('\n', f);
    fclose(f);
}

static const uint8_t *Find(const uint8_t *hay, size_t n, const char *needle) {
    size_t m = strlen(needle);
    for (size_t i = 0; i + m <= n; i++)
        if (hay[i] == (uint8_t)needle[0] && memcmp(hay + i, needle, m) == 0) return hay + i;
    return NULL;
}

static int Printable(const uint8_t *p, uint32_t n) {
    for (uint32_t i = 0; i < n; i++)
        if (p[i] < 0x20 || p[i] > 0x7E) return 0;
    return 1;
}

// Recognise the item table by its first record: u32 len, ASCII name, u32 key, u8 0,
// u16 max_endurance, u64 max_stack_count, then 01 02. Other tables (multichangeinfo,
// skill, ...) also contain Item_*AbyssGear* names but never start like this.
static int LooksLikeItemTable(const uint8_t *d, uint32_t size) {
    if (size < (1u << 20) || size > (256u << 20)) return 0;
    uint32_t len;
    memcpy(&len, d, 4);
    if (len < 1 || len > 128 || !Printable(d + 4, len)) return 0;
    const uint8_t *rec = d + 4 + len;
    uint64_t stack;
    memcpy(&stack, rec + 7, 8);
    return rec[4] == 0 && stack >= 1 && stack <= 10000000 && rec[15] == 1 && rec[16] == 2;
}

static void PatchItemInfo(uint8_t *d, uint32_t size) {
    const uint8_t *end = d + size;
    int patched = 0, already = 0;
    const uint8_t *p = d;
    while ((p = Find(p, (size_t)(end - p), "AbyssGear_")) != NULL) {
        const uint8_t *next = p + 10;
        // Walk back to the start of the record name (its u32 length sits just before it).
        for (int back = 1; back <= 96; back++) {
            const uint8_t *s = p - back;
            if (s - 4 < d) break;
            uint32_t len;
            memcpy(&len, s - 4, 4);
            if (len < 10 || len > 128 || s + len + 7 > end || p + 10 > s + len) continue;
            if (memcmp(s, "Item_", 5) != 0 || !Printable(s, len)) continue;
            uint8_t *rec = (uint8_t *)s + len;
            uint32_t key;
            uint16_t endurance;
            memcpy(&key, rec, 4);
            memcpy(&endurance, rec + 5, 2);
            if (rec[4] != 0 || key == 0) break;
            if (endurance == 0xFFFF) {
                already++;
            } else {
                Log("  %-60.*s key=%u  %u -> 65535", (int)len, (const char *)s, key, endurance);
                endurance = 0xFFFF;
                memcpy(rec + 5, &endurance, 2);
                patched++;
            }
            next = rec + 7;
            break;
        }
        p = next;
    }
    if (patched || already) {
        InterlockedIncrement(&g_patched_tables);
        Log("item table (%u bytes): %d Abyss Gear item(s) made unbreakable, %d already unlimited",
            size, patched, already);
    }
}

static uint8_t Detour(void *a, void *b, ReadBuf *buf, void *c, uint64_t e, uint64_t f, uint64_t g) {
    uint8_t ok = g_orig(a, b, buf, c, e, f, g);
    if (ok && g_enabled && buf && buf->data && LooksLikeItemTable(buf->data, buf->size) &&
        Find(buf->data, buf->size, "AbyssGear_")) {
        PatchItemInfo(buf->data, buf->size);
    }
    return ok;
}

static int ParseSig(const char *s, uint8_t *bytes, uint8_t *mask) {
    int n = 0;
    while (*s) {
        if (*s == ' ') { s++; continue; }
        if (s[0] == '?') { bytes[n] = 0; mask[n++] = 0; s += 2; continue; }
        unsigned v;
        sscanf(s, "%2x", &v);
        bytes[n] = (uint8_t)v;
        mask[n++] = 1;
        s += 2;
    }
    return n;
}

static uint8_t *ScanModule(HMODULE mod) {
    uint8_t bytes[128], mask[128];
    int n = ParseSig(SIG, bytes, mask);
    uint8_t *base = (uint8_t *)mod;
    IMAGE_NT_HEADERS *nt = (IMAGE_NT_HEADERS *)(base + ((IMAGE_DOS_HEADER *)base)->e_lfanew);
    IMAGE_SECTION_HEADER *sec = IMAGE_FIRST_SECTION(nt);
    uint8_t *found = NULL;
    int count = 0;
    for (int i = 0; i < nt->FileHeader.NumberOfSections; i++) {
        if (!(sec[i].Characteristics & IMAGE_SCN_MEM_EXECUTE)) continue;
        uint8_t *p = base + sec[i].VirtualAddress, *e = p + sec[i].Misc.VirtualSize;
        while (p < e) {
            MEMORY_BASIC_INFORMATION mbi;
            if (!VirtualQuery(p, &mbi, sizeof mbi)) break;
            uint8_t *rend = (uint8_t *)mbi.BaseAddress + mbi.RegionSize;
            if (rend > e) rend = e;
            DWORD prot = mbi.Protect & 0xFF;
            int readable = mbi.State == MEM_COMMIT && !(mbi.Protect & PAGE_GUARD) &&
                           (prot == PAGE_EXECUTE_READ || prot == PAGE_EXECUTE_READWRITE ||
                            prot == PAGE_READONLY || prot == PAGE_READWRITE ||
                            prot == PAGE_EXECUTE_WRITECOPY || prot == PAGE_WRITECOPY);
            if (readable) {
                for (uint8_t *q = p; q + n <= rend; q++) {
                    if (*q != bytes[0]) continue;
                    int k = 1;
                    while (k < n && (!mask[k] || q[k] == bytes[k])) k++;
                    if (k == n) { found = q; count++; }
                }
            }
            p = rend;
        }
    }
    if (count > 1) {
        Log("ERROR: reader signature matched %d places, refusing to hook", count);
        return NULL;
    }
    return found ? found - SIG_OFFSET : NULL;
}

static uint8_t *AllocNear(uint8_t *target) {
    SYSTEM_INFO si;
    GetSystemInfo(&si);
    uintptr_t gran = si.dwAllocationGranularity;
    uintptr_t start = (uintptr_t)target & ~(gran - 1);
    for (uintptr_t dist = gran; dist < 0x70000000; dist += gran) {
        void *p = VirtualAlloc((void *)(start - dist), 0x1000, MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE);
        if (p) return p;
        p = VirtualAlloc((void *)(start + dist), 0x1000, MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE);
        if (p) return p;
    }
    return NULL;
}

static uint8_t *WriteAbsJmp(uint8_t *at, void *to) {
    at[0] = 0xFF; at[1] = 0x25;
    memset(at + 2, 0, 4);
    memcpy(at + 6, &to, 8);
    return at + 14;
}

static int InstallHook(uint8_t *fn) {
    uint8_t *page = AllocNear(fn);
    if (!page) { Log("ERROR: no memory near the reader for the hook"); return 0; }
    uint8_t *relay = page, *tramp = page + 32;
    const char *how;
    Log("reader starts with %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X",
        fn[0], fn[1], fn[2], fn[3], fn[4], fn[5], fn[6], fn[7], fn[8], fn[9], fn[10], fn[11], fn[12], fn[13]);

    if (memcmp(fn, PROLOGUE, 5) == 0) {
        memcpy(tramp, PROLOGUE, 5);
        WriteAbsJmp(tramp + 5, fn + 5);
        how = "";
    } else if (fn[0] == 0xE9) {
        int32_t rel;
        memcpy(&rel, fn + 1, 4);
        WriteAbsJmp(tramp, fn + 5 + rel);
        how = " (another plugin had hooked it first; chained onto its hook)";
    } else if (fn[0] == 0xFF && fn[1] == 0x25) {
        int32_t rel;
        void *target;
        memcpy(&rel, fn + 2, 4);
        memcpy(&target, fn + 6 + rel, 8);
        WriteAbsJmp(tramp, target);
        how = " (another plugin had hooked it first; chained onto its hook)";
    } else if (fn[0] == 0x48 && fn[1] == 0xB8 && fn[10] == 0xFF && fn[11] == 0xE0) {
        // mov rax, imm64; jmp rax  (Pack Rat's hook)
        void *target;
        memcpy(&target, fn + 2, 8);
        WriteAbsJmp(tramp, target);
        how = " (another plugin had hooked it first; chained onto its hook)";
    } else {
        Log("ERROR: unexpected bytes at the reader (%02X %02X %02X %02X %02X), not hooking",
            fn[0], fn[1], fn[2], fn[3], fn[4]);
        VirtualFree(page, 0, MEM_RELEASE);
        return 0;
    }
    WriteAbsJmp(relay, (void *)Detour);
    g_orig = (ReadFn)tramp;
    FlushInstructionCache(GetCurrentProcess(), page, 0x1000);

    // Swap the first 5 bytes for "jmp relay" in one 8-byte atomic write.
    DWORD old;
    if (!VirtualProtect(fn, 8, PAGE_EXECUTE_READWRITE, &old)) {
        Log("ERROR: VirtualProtect failed (%lu)", GetLastError());
        return 0;
    }
    uint8_t patch[8];
    memcpy(patch, fn, 8);
    int32_t rel = (int32_t)(relay - (fn + 5));
    patch[0] = 0xE9;
    memcpy(patch + 1, &rel, 4);
    LONG64 v;
    memcpy(&v, patch, 8);
    InterlockedExchange64((volatile LONG64 *)fn, v);
    VirtualProtect(fn, 8, old, &old);
    FlushInstructionCache(GetCurrentProcess(), fn, 8);
    Log("file reader hooked at +0x%llX%s", (unsigned long long)(fn - (uint8_t *)GetModuleHandleW(NULL)), how);
    return 1;
}

static DWORD WINAPI Init(LPVOID unused) {
    (void)unused;
    HMODULE game = GetModuleHandleW(NULL);
    for (int attempt = 0; attempt < 60; attempt++) {
        uint8_t *fn = ScanModule(game);
        if (fn) {
            InstallHook(fn);
            return 0;
        }
        if (attempt == 0) Log("file reader not found yet; retrying for a minute");
        Sleep(1000);
    }
    Log("ERROR: the game's file reader was not found - the game was probably updated. Nothing was changed.");
    return 0;
}

BOOL WINAPI DllMain(HINSTANCE inst, DWORD reason, LPVOID reserved) {
    (void)reserved;
    if (reason != DLL_PROCESS_ATTACH) return TRUE;
    DisableThreadLibraryCalls(inst);

    char exe[MAX_PATH];
    GetModuleFileNameA(NULL, exe, MAX_PATH);
    const char *exe_name = strrchr(exe, '\\') ? strrchr(exe, '\\') + 1 : exe;
    if (_stricmp(exe_name, "CrimsonDesert.exe") != 0) return TRUE;

    GetModuleFileNameA(inst, g_log_path, MAX_PATH);
    char *dot = strrchr(g_log_path, '.');
    if (dot) *dot = 0;
    snprintf(g_ini_path, MAX_PATH, "%s.ini", g_log_path);
    strncat(g_log_path, ".log", MAX_PATH - strlen(g_log_path) - 1);

    g_enabled = GetPrivateProfileIntA("AbyssDurability", "Enabled", 1, g_ini_path) != 0;
    Log("AbyssDurability 1.0 loaded (pid %lu, enabled %d)", GetCurrentProcessId(), g_enabled);
    if (g_enabled) {
        HANDLE t = CreateThread(NULL, 0, Init, NULL, 0, NULL);
        if (t) CloseHandle(t);
    }
    return TRUE;
}
