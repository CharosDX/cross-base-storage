// Cross-Base Storage
//
// Magic chests in all your bases work as one storage for building and crafting. The game keeps a list of crafting
// stock entities and item counts per base and only looks at the base the player (or the build position) is in. This
// DLL redirects a few calls so they see all bases, own base first:
//   - the stock list (FUN_140468660) in the building tool, the material consumption and the station queue fill,
//   - the build menu counts (FUN_1404680b0),
//   - the item count of a base (FUN_140469610) in the build check.
// From other bases only magic chests are used, see stock_wrapper.
// Optionally (cross-base-storage.ini, on by default) the consumption takes from the inventory first.
//
// All places are found by byte patterns and checked before patching; any mismatch leaves the game untouched.
#include <windows.h>
#include <stdint.h>
#include <stdio.h>
#include <stdarg.h>
#include <stdlib.h>

// Byte patterns ("??" = any byte), taken from game build 1273043 (release_1.0, 2026-10-20).
// FUN_140468660: stock list of the base around a position.
static const char STOCK_FUNCTION_PATTERN[] =
    "48 89 5C 24 08 48 89 6C 24 10 48 89 74 24 20 57 48 83 EC 40 49 8B F9 49 8B E8 48 8B F2 48 8B D9 4D 85 C0";
// FUN_1404680b0: build menu count rebuild (reads the table at 0x16d0/0x16d4).
static const char COUNTS_FUNCTION_PATTERN[] =
    "48 89 4C 24 08 53 55 56 57 48 83 EC 58 8B 82 D4 16 00 00 49 8B D8 2B 82 D0 16 00 00";
// FUN_140469610: item count of the base at a position (count maps at 0x1610).
static const char BASE_COUNT_FUNCTION_PATTERN[] =
    "48 89 5C 24 08 48 89 74 24 10 57 48 83 EC 40 41 8B D8 48 8B FA 4C 8B D1 48 85 C9 0F 84 ?? ?? ?? ?? "
    "48 8B B9 20 16 00 00";
// FUN_140469060: finds the base slot that contains a position (only called, not patched).
static const char LOOKUP_FUNCTION_PATTERN[] =
    "48 89 5C 24 08 57 48 83 EC 10 8B 7A 24 45 33 DB 48 8B DA 4C 8B D1 48 85 FF 0F 84 ?? ?? ?? ?? "
    "F2 41 0F 10 00 41 8B 40 08 4C 8D 82 24 01 00 00";
// FUN_1409a7570(result, world, component type hash, entity): result[0] = the entity's component or NULL.
static const char COMPONENT_FUNCTION_PATTERN[] =
    "48 89 5C 24 08 48 89 74 24 10 57 48 83 EC 20 48 8D BA ?? ?? ?? ?? 44 89 44 24 40 48 8B F1 48 8D 54 24 40 "
    "48 8B 0F 41 8B D9 48 81 C1 88 00 00 00";
// Calls of the stock function in the building tool and in the material consumption.
static const char BUILD_TOOL_CALL_PATTERN[] =
    "F3 0F 11 85 ?? ?? ?? ?? 48 8D 95 ?? ?? ?? ?? 48 8D 8D ?? ?? ?? ?? E8 ?? ?? ?? ?? 48 8B 85";
#define BUILD_TOOL_CALL_OFFSET 22
static const char CONSUME_CALL_PATTERN[] =
    "4C 89 AC 24 80 00 00 00 F2 0F 11 44 24 30 E8 ?? ?? ?? ?? 49 8B 4F 08";
#define CONSUME_CALL_OFFSET 14
// Call of the stock function in the station queue fill (player_crafting AutomaticTransfer, FUN_1402c1a80).
static const char QUEUE_CALL_PATTERN[] =
    "4C 89 64 24 70 4C 89 64 24 78 4C 89 65 80 4C 89 65 88 F2 0F 11 44 24 40 E8 ?? ?? ?? ?? 48 8B 7C 24 70";
#define QUEUE_CALL_OFFSET 24
// Call of the count rebuild in FUN_1402af3b0.
static const char COUNTS_CALL_PATTERN[] = "F3 0F 59 C2 F3 0F 11 44 24 34 E8 ?? ?? ?? ?? 4C 8B 7D";
#define COUNTS_CALL_OFFSET 10
// The two calls of the base count function in the build tool check FUN_1402865e0.
static const char BASE_COUNT_CALL_PATTERN[] = "F3 0F 11 44 24 44 E8 ?? ?? ?? ?? 03 C3";
#define BASE_COUNT_CALL_OFFSET 6
#define BASE_COUNT_CALLS 2
// Order check in the material consumption FUN_1403f1ae0. Its mode byte at tx + 0xb1 is 1 (base stock first, then the
// inventory; building) or 2 (inventory first; stations). The jump skips the inventory step unless the mode is 2, so
// replacing it with NOPs takes from the inventory first in both modes.
static const char INVENTORY_FIRST_PATTERN[] = "41 80 BE B1 00 00 00 02 8B 1B 75 31";
#define INVENTORY_FIRST_JUMP_OFFSET 10
// FUN_1404664a0: the client's predicted update of the base counts when a block is placed. It subtracts the whole cost
// from the base counts, which is only right when the chests are used first.
static const char PREDICT_FUNCTION_PATTERN[] =
    "48 85 C9 0F 84 ?? ?? ?? ?? 48 89 4C 24 08 53 55 41 54 48 83 EC 50 F2 41 0F 10 01 4C 8B E2";
// Its only call, in the material provider FUN_14048eeb0.
static const char PREDICT_CALL_PATTERN[] =
    "F3 0F 11 4C 24 58 F3 0F 59 C2 F3 0F 11 44 24 54 E8 ?? ?? ?? ?? B0 01 48 83 C4 78";
#define PREDICT_CALL_OFFSET 16

// Qualified name hash of keen::ecs::InventoryEmptyState (component type id, see .cache/types.json). Among the
// crafting stock entities only magic chests have it at runtime, not Craft From Stations stations and not player
// planted fruit bushes (checked in game; DefaultInventory is only in the template and gone after placing).
#define CHEST_ONLY_TYPE 0x37c4cfe7u
// Component storage of a transaction: **tx + 0x30 (FUN_14040a400 passes FUN_1409b43d0(*(*tx)) to its component
// lookups). It lies inside the world at the offset that FUN_1409a7570 adds itself ("lea rdi, [rdx + offset]").
#define CONTEXT_STORAGE 0x30
#define COMPONENT_STORAGE_OFFSET_AT 18

#define MAX_PATTERN 64

// Base table layout (from FUN_140468660, FUN_140469060 and FUN_140469610).
#define TABLE_COUNT 0x24      // u32 number of used slots
#define TABLE_SLOTS 0x30      // first slot, SLOT_SIZE bytes each; the stock list {ids, count} is at its start
#define SLOT_SIZE 0x130
#define COUNT_MAP 0x1610      // count map of slot i at COUNT_MAP + i * COUNT_MAP_STRIDE
#define COUNT_MAP_STRIDE 0x58
#define MAX_SLOTS 16
#define MAX_TABLES 4
#define MAX_MERGED 4096
// Replaced merged buffers are freed only after this many later rebuilds.
#define RETIRED_KEEP 64

typedef struct IdList {
    uint32_t *ids;
    uint64_t count;
} IdList;

typedef struct BaseLists {
    IdList stock;
    IdList other;
} BaseLists;

// Result of FUN_140469060: status byte (0 = inside a base), slot index at offset 8.
typedef struct LookupResult {
    uint8_t status;
    uint8_t pad[7];
    uint8_t slot;
    uint8_t pad2[7];
} LookupResult;

typedef uint8_t *(*StockFn)(uint8_t *result, BaseLists *out, uint8_t *table_a, uint8_t *table_b, float *position,
    uint32_t player);
typedef void *(*CountsFn)(void *result, uint8_t *table, float *position, uint32_t player);
typedef uint16_t (*BaseCountFn)(uint8_t *map, uint8_t *table, uint32_t item, float *position);
typedef LookupResult *(*LookupFn)(LookupResult *result, uint8_t *table, float *position);
typedef void **(*ComponentFn)(void **result, uint8_t *world, uint32_t type, uint32_t entity);
// Cost list of a placement: count entries of 0x18 bytes (item at +0x10, amount at +0x14).
typedef struct CostList {
    void *items;
    uint64_t count;
} CostList;
typedef void (*PredictFn)(uint8_t *table, void *context, CostList *costs, float *position, char subtract);

// Merged list for one current base, valid while the table still has the same lists.
typedef struct Merged {
    IdList lists[MAX_SLOTS];
    uint32_t sums[MAX_SLOTS];
    uint32_t slot_count;
    uint32_t *ids;
    uint64_t count;
} Merged;

static StockFn original_stock;
static CountsFn original_counts;
static BaseCountFn original_base_count;
static LookupFn lookup;
static ComponentFn get_component;
static PredictFn original_predict;
static uint32_t storage_offset;
// Transaction of the material consumption that is about to ask for the stock list, set by the consume call stub.
static __declspec(thread) uint8_t *current_tx;
static __declspec(thread) uint32_t consume_ids[MAX_MERGED];
static char log_path[MAX_PATH];
static char settings_path[MAX_PATH];
// The first line of a game start replaces the log of the previous one, so the file never grows across sessions.
static int log_fresh = 1;
static SRWLOCK lock = SRWLOCK_INIT;
static uint8_t *cache_tables[MAX_TABLES];
static Merged merged[MAX_TABLES][MAX_SLOTS];
static uint32_t *retired[RETIRED_KEEP];
static uint32_t retired_next;

static void log_line(const char *format, ...)
{
    if (!log_path[0]) return;
    char line[512];
    SYSTEMTIME time;
    GetLocalTime(&time);
    int used = sprintf_s(line, sizeof(line), "%02d:%02d:%02d ", time.wHour, time.wMinute, time.wSecond);
    va_list args;
    va_start(args, format);
    int written = vsprintf_s(line + used, sizeof(line) - used - 2, format, args);
    va_end(args);
    if (used < 0 || written < 0) return;
    used += written;
    line[used++] = '\r';
    line[used++] = '\n';
    HANDLE file = CreateFileA(log_path, log_fresh ? GENERIC_WRITE : FILE_APPEND_DATA, FILE_SHARE_READ, NULL,
        log_fresh ? CREATE_ALWAYS : OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (file == INVALID_HANDLE_VALUE) return;
    log_fresh = 0;
    DWORD done;
    WriteFile(file, line, (DWORD)used, &done, NULL);
    CloseHandle(file);
}

static IdList *slot_stock(uint8_t *table, uint32_t slot)
{
    return (IdList *)(table + TABLE_SLOTS + slot * SLOT_SIZE);
}

static uint32_t slot_count_of(uint8_t *table)
{
    uint32_t count = *(uint32_t *)(table + TABLE_COUNT);
    return count > MAX_SLOTS ? MAX_SLOTS : count;
}

// Checksum of a list's ids, so a list that keeps its buffer but changes content is noticed.
static uint32_t list_sum(const IdList *list)
{
    uint32_t sum = 2166136261u;
    for (uint64_t i = 0; list->ids && i < list->count && i < MAX_MERGED; i++) sum = (sum ^ list->ids[i]) * 16777619u;
    return sum;
}

static int same_lists(const Merged *cache, uint8_t *table, uint32_t slot_count)
{
    if (!cache->ids || cache->slot_count != slot_count) return 0;
    for (uint32_t i = 0; i < slot_count; i++) {
        IdList *list = slot_stock(table, i);
        if (list->ids != cache->lists[i].ids || list->count != cache->lists[i].count
            || list_sum(list) != cache->sums[i]) return 0;
    }
    return 1;
}

// Builds the merged list for base `current`: its own stock first, then all other bases. Called under the lock.
static void rebuild(Merged *cache, uint8_t *table, uint32_t slot_count, uint32_t current)
{
    uint32_t *ids = malloc(MAX_MERGED * sizeof(uint32_t));
    if (!ids) return;
    uint64_t count = 0;
    for (uint32_t pass = 0; pass <= slot_count; pass++) {
        // pass 0 is the current base, then every slot in order except the current one.
        uint32_t i = pass == 0 ? current : pass - 1;
        if (pass > 0 && i == current) continue;
        IdList *list = slot_stock(table, i);
        uint64_t take = list->ids ? list->count : 0;
        if (take > MAX_MERGED - count) take = MAX_MERGED - count;
        memcpy(ids + count, list->ids, take * sizeof(uint32_t));
        count += take;
    }
    for (uint32_t i = 0; i < slot_count; i++) {
        cache->lists[i] = *slot_stock(table, i);
        cache->sums[i] = list_sum(&cache->lists[i]);
    }
    cache->slot_count = slot_count;
    if (cache->ids) {
        free(retired[retired_next]);
        retired[retired_next] = cache->ids;
        retired_next = (retired_next + 1) % RETIRED_KEEP;
    }
    cache->ids = ids;
    cache->count = count;
}

// Returns the up to date merged list of a table's base slot, or NULL. Called under the lock.
static Merged *merged_for(uint8_t *table, uint32_t slot)
{
    uint32_t t = 0;
    while (t < MAX_TABLES && cache_tables[t] && cache_tables[t] != table) t++;
    if (t == MAX_TABLES) return NULL;
    cache_tables[t] = table;
    uint32_t slot_count = slot_count_of(table);
    if (slot >= slot_count) return NULL;
    Merged *cache = &merged[t][slot];
    if (!same_lists(cache, table, slot_count)) rebuild(cache, table, slot_count, slot);
    return cache->ids ? cache : NULL;
}

// Stock list calls: replace the current base's list with the merged list of all bases. For the material consumption
// and the queue fill (tx set by the consume call stub) other bases only give from magic chests: Craft From Stations
// stations and player planted fruit bushes far away are not sent to the players, so they would be used without ever
// being shown.
static uint8_t *stock_wrapper(uint8_t *result, BaseLists *out, uint8_t *table_a, uint8_t *table_b, float *position,
    uint32_t player)
{
    uint8_t *tx = current_tx;
    current_tx = NULL;
    uint8_t *returned = original_stock(result, out, table_a, table_b, position, player);
    uint8_t *table = table_a ? table_a : table_b;
    if (*returned != 0 || !table || !out->stock.ids) return returned;

    AcquireSRWLockExclusive(&lock);
    __try {
        int current = -1;
        uint32_t slot_count = slot_count_of(table);
        for (uint32_t i = 0; i < slot_count && current < 0; i++) {
            IdList *list = slot_stock(table, i);
            if (list->ids == out->stock.ids && list->count == out->stock.count) current = (int)i;
        }
        Merged *cache = current >= 0 ? merged_for(table, (uint32_t)current) : NULL;
        if (cache) {
            out->stock.ids = cache->ids;
            out->stock.count = cache->count;
            if (tx) {
                uint8_t *world = *(uint8_t **)(*(uint8_t **)*(uint8_t **)tx + CONTEXT_STORAGE) - storage_offset;
                uint64_t own = slot_stock(table, (uint32_t)current)->count, kept = 0;
                for (uint64_t i = 0; i < cache->count && kept < MAX_MERGED; i++) {
                    void *component = NULL;
                    if (i < own || *get_component(&component, world, CHEST_ONLY_TYPE, cache->ids[i]))
                        consume_ids[kept++] = cache->ids[i];
                }
                out->stock.ids = consume_ids;
                out->stock.count = kept;
            }
        }
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        static int warned;
        if (!warned++) log_line("reading the stock lists failed, list left unmerged or unfiltered");
    }
    ReleaseSRWLockExclusive(&lock);
    return returned;
}

static void remember_tx(uint8_t *tx)
{
    current_tx = tx;
}

// Build menu counts: run the original with the merged list in place of the current base's stock list.
static void *counts_wrapper(void *result, uint8_t *table, float *position, uint32_t player)
{
    IdList *swapped = NULL, saved = {0};
    AcquireSRWLockExclusive(&lock);
    __try {
        LookupResult found;
        if (table && lookup(&found, table, position)->status == 0) {
            Merged *cache = merged_for(table, found.slot);
            if (cache) {
                swapped = slot_stock(table, found.slot);
                saved = *swapped;
                swapped->ids = cache->ids;
                swapped->count = cache->count;
            }
        }
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        swapped = NULL;
    }
    void *returned = original_counts(result, table, position, player);
    if (swapped) *swapped = saved;
    ReleaseSRWLockExclusive(&lock);
    return returned;
}

// Only tables with a count map per slot are summed; skip anything that does not look like a hash map.
static int sane_count_map(uint8_t *map)
{
    uint64_t capacity = *(uint64_t *)(map + 0x10);
    uint32_t entries = *(uint32_t *)(map + 0x54);
    return capacity && capacity <= 65536 && (capacity & (capacity - 1)) == 0 && entries <= capacity
        && *(void **)(map + 0x08) && *(void **)(map + 0x28) && *(void **)(map + 0x40);
}

// Build tool check: without a given map, count the item in all bases. This uses the server's per base maps, which
// include far stations: building blocks lying only in a far station pass the check but are refused by the server
// (blocks are crafted into the inventory and kept in chests, so this hardly happens).
static uint16_t base_count_wrapper(uint8_t *map, uint8_t *table, uint32_t item, float *position)
{
    uint16_t own = original_base_count(map, table, item, position);
    if (map || !table) return own;
    uint32_t total = 0;
    __try {
        LookupResult found;
        if (lookup(&found, table, position)->status != 0) return own;
        // Add up the per base count maps of all bases. They are the server's real counts; the build menu counts
        // cannot be used here because on the host the client side prediction has already taken the material off
        // them when the server side check runs.
        uint32_t slot_count = slot_count_of(table);
        // A non-null map argument makes the original read its offsets relative to that pointer, so
        // table + i * COUNT_MAP_STRIDE selects slot i's count map.
        for (uint32_t i = 0; i < slot_count; i++) {
            uint8_t *slot_base = table + i * COUNT_MAP_STRIDE;
            if (sane_count_map(slot_base + COUNT_MAP)) total += original_base_count(slot_base, table, item, position);
        }
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return own;
    }
    return total > 0xffff ? 0xffff : (uint16_t)total;
}

// With inventory first, the prediction would take the cost off the base counts while the server takes it from the
// inventory: the menu briefly showed the cost subtracted twice. An empty cost list keeps the rest of the original,
// which restarts the count rebuild, so the menu shows the real counts a moment later.
static void predict_wrapper(uint8_t *table, void *context, CostList *costs, float *position, char subtract)
{
    CostList none = {costs ? costs->items : NULL, 0};
    original_predict(table, context, &none, position, subtract);
}

// Allocates one page within +-2 GB of the game image, so rel32 calls can reach it.
static uint8_t *alloc_near(uint8_t *image, SIZE_T image_size)
{
    SYSTEM_INFO info;
    GetSystemInfo(&info);
    uintptr_t granularity = info.dwAllocationGranularity;
    uintptr_t start = ((uintptr_t)image + image_size + granularity) & ~(granularity - 1);
    for (uintptr_t address = start; address < (uintptr_t)image + 0x70000000; address += granularity) {
        void *page = VirtualAlloc((void *)address, 4096, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
        if (page) return page;
    }
    return NULL;
}

// Makes the stub page executable and read-only once the stubs are written.
static int seal_stubs(uint8_t *stubs)
{
    DWORD old;
    if (!VirtualProtect(stubs, 4096, PAGE_EXECUTE_READ, &old)) return 0;
    FlushInstructionCache(GetCurrentProcess(), stubs, 4096);
    return 1;
}

// Parses a pattern like "E8 ?? 8B" into bytes and a mask (0 = any byte). Returns the length or 0 on error.
static int parse_pattern(const char *text, uint8_t *bytes, uint8_t *mask)
{
    int length = 0;
    while (*text) {
        if (*text == ' ') { text++; continue; }
        if (length >= MAX_PATTERN || !text[1]) return 0;
        if (text[0] == '?' && text[1] == '?') {
            bytes[length] = 0;
            mask[length] = 0;
        } else {
            char hex[3] = {text[0], text[1], 0};
            char *end;
            bytes[length] = (uint8_t)strtoul(hex, &end, 16);
            if (*end) return 0;
            mask[length] = 1;
        }
        length++;
        text += 2;
    }
    return length;
}

static int matches_at(const uint8_t *at, const uint8_t *bytes, const uint8_t *mask, int length)
{
    for (int i = 0; i < length; i++) {
        if (mask[i] && at[i] != bytes[i]) return 0;
    }
    return 1;
}

// Finds up to `max` matches of a pattern in [start, end) and returns how many there are in total.
static int find_all(uint8_t *start, uint8_t *end, const char *pattern, uint8_t **found, int max)
{
    uint8_t bytes[MAX_PATTERN], mask[MAX_PATTERN];
    int length = parse_pattern(pattern, bytes, mask);
    int count = 0;
    if (length == 0 || end - start < length) return 0;
    for (uint8_t *at = start; at <= end - length; at++) {
        if (at[0] == bytes[0] && matches_at(at, bytes, mask, length)) {
            if (count < max) found[count] = at;
            count++;
        }
    }
    return count;
}

static uint8_t *find_unique(uint8_t *start, uint8_t *end, const char *pattern)
{
    uint8_t *found;
    return find_all(start, end, pattern, &found, 1) == 1 ? found : NULL;
}

// Returns the target of the "call rel32" at call, or NULL if it isn't one.
static uint8_t *call_target(uint8_t *call)
{
    if (call[0] != 0xE8) return NULL;
    return call + 5 + *(int32_t *)(call + 1);
}

// Writes "jmp [rip+0]; dq target" into the stub.
static void write_jump(uint8_t *stub, void *target)
{
    stub[0] = 0xFF;
    stub[1] = 0x25;
    *(uint32_t *)(stub + 2) = 0;
    *(void **)(stub + 6) = target;
}

// Stub for the consumption and queue fill calls: FUN_1403f1ae0 and FUN_1402c1a80 both keep their transaction in r14
// when they ask for the stock list. The stub saves the argument registers, passes r14 to remember_tx, restores them
// and jumps to stock_wrapper.
static void write_consume_stub(uint8_t *stub)
{
    static const uint8_t CODE[] = {
        0x51, 0x52, 0x41, 0x50, 0x41, 0x51,        // push rcx, rdx, r8, r9
        0x48, 0x83, 0xEC, 0x28,                    // sub rsp, 0x28
        0x4C, 0x89, 0xF1,                          // mov rcx, r14
        0xFF, 0x15, 0x15, 0x00, 0x00, 0x00,        // call [rip+0x15] -> remember_tx
        0x48, 0x83, 0xC4, 0x28,                    // add rsp, 0x28
        0x41, 0x59, 0x41, 0x58, 0x5A, 0x59,        // pop r9, r8, rdx, rcx
        0xFF, 0x25, 0x0D, 0x00, 0x00, 0x00,        // jmp [rip+0xd] -> stock_wrapper
    };
    memcpy(stub, CODE, sizeof(CODE));
    // The code is 35 bytes; the call at 13 reads its target at 19 + 0x15 = 40, the jmp at 29 reads 35 + 0xd = 48.
    *(void **)(stub + 40) = (void *)remember_tx;
    *(void **)(stub + 48) = (void *)stock_wrapper;
}

// Points the call to the stub.
static int redirect_call(uint8_t *call, uint8_t *stub)
{
    int64_t rel = (int64_t)(stub - (call + 5));
    if (rel < INT32_MIN || rel > INT32_MAX) return 0;
    DWORD old;
    if (!VirtualProtect(call, 5, PAGE_EXECUTE_READWRITE, &old)) return 0;
    *(int32_t *)(call + 1) = (int32_t)rel;
    VirtualProtect(call, 5, old, &old);
    FlushInstructionCache(GetCurrentProcess(), call, 5);
    return 1;
}

// Returns the executable ".text" section of the game image.
static int code_section(uint8_t *image, IMAGE_NT_HEADERS64 *nt, uint8_t **start, uint8_t **end)
{
    IMAGE_SECTION_HEADER *section = IMAGE_FIRST_SECTION(nt);
    for (WORD i = 0; i < nt->FileHeader.NumberOfSections; i++, section++) {
        if (memcmp(section->Name, ".text", 6) == 0) {
            *start = image + section->VirtualAddress;
            *end = *start + section->Misc.VirtualSize;
            return 1;
        }
    }
    return 0;
}

// Finds a call by pattern; it must match exactly once and call `function`.
static uint8_t *find_call(uint8_t *code, uint8_t *code_end, const char *pattern, int offset, uint8_t *function,
    const char *name)
{
    uint8_t *match = find_unique(code, code_end, pattern);
    uint8_t *call = match ? match + offset : NULL;
    if (!call || call_target(call) != function) {
        log_line("%s call not found (or calls something else), mod NOT active", name);
        return NULL;
    }
    return call;
}

// Reads inventory_first from cross-base-storage.ini next to the DLL and writes the file with the default if it is
// missing. The archive doesn't contain it, so an update never resets the player's choice.
static int inventory_first_enabled(void)
{
    static const char DEFAULT_SETTINGS[] =
        "; Cross-Base Storage settings. Restart the game after changing them.\r\n"
        "[settings]\r\n"
        "; 1 = take materials from your inventory first, then from the magic chests.\r\n"
        "; 0 = magic chests first, then your inventory (like the base game when building).\r\n"
        "inventory_first=1\r\n";
    if (!settings_path[0]) return 1;
    HANDLE file = CreateFileA(settings_path, GENERIC_WRITE, 0, NULL, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, NULL);
    if (file != INVALID_HANDLE_VALUE) {
        DWORD done;
        WriteFile(file, DEFAULT_SETTINGS, sizeof(DEFAULT_SETTINGS) - 1, &done, NULL);
        CloseHandle(file);
    }
    return GetPrivateProfileIntA("settings", "inventory_first", 1, settings_path) != 0;
}

// Finds the order check and the prediction call for inventory first. Optional: if either isn't found, neither is
// patched and the rest of the mod works with the game's own order. Returns 0 then.
static int find_inventory_first(uint8_t *code, uint8_t *code_end, uint8_t **order_jump, uint8_t **predict_call)
{
    if (!inventory_first_enabled()) {
        log_line("inventory first off (cross-base-storage.ini), chests are used first when building");
        return 0;
    }
    uint8_t *order = find_unique(code, code_end, INVENTORY_FIRST_PATTERN);
    uint8_t *predict = find_unique(code, code_end, PREDICT_FUNCTION_PATTERN);
    uint8_t *call = find_unique(code, code_end, PREDICT_CALL_PATTERN);
    if (!order || !predict || !call || call_target(call + PREDICT_CALL_OFFSET) != predict) {
        log_line("inventory first: game code not found (order %d, prediction %d, call %d), chests are used first "
            "when building", order != NULL, predict != NULL, call != NULL);
        return 0;
    }
    *order_jump = order + INVENTORY_FIRST_JUMP_OFFSET;
    *predict_call = call + PREDICT_CALL_OFFSET;
    original_predict = (PredictFn)predict;
    return 1;
}

// Removes the jump that skips the inventory step unless the mode is 2.
static int remove_order_jump(uint8_t *jump)
{
    DWORD old;
    if (!VirtualProtect(jump, 2, PAGE_EXECUTE_READWRITE, &old)) return 0;
    jump[0] = 0x90;
    jump[1] = 0x90;
    VirtualProtect(jump, 2, old, &old);
    FlushInstructionCache(GetCurrentProcess(), jump, 2);
    return 1;
}

static void install(void)
{
    uint8_t *image = (uint8_t *)GetModuleHandleA(NULL);
    IMAGE_NT_HEADERS64 *nt = (IMAGE_NT_HEADERS64 *)(image + ((IMAGE_DOS_HEADER *)image)->e_lfanew);
    log_line("game timestamp %08lx, image size %08lx", nt->FileHeader.TimeDateStamp, nt->OptionalHeader.SizeOfImage);
    uint8_t *code, *code_end;
    if (!code_section(image, nt, &code, &code_end)) {
        log_line("no code section found, mod NOT active");
        return;
    }

    uint8_t *stock = find_unique(code, code_end, STOCK_FUNCTION_PATTERN);
    uint8_t *counts = find_unique(code, code_end, COUNTS_FUNCTION_PATTERN);
    uint8_t *base_count = find_unique(code, code_end, BASE_COUNT_FUNCTION_PATTERN);
    uint8_t *lookup_function = find_unique(code, code_end, LOOKUP_FUNCTION_PATTERN);
    uint8_t *component_function = find_unique(code, code_end, COMPONENT_FUNCTION_PATTERN);
    if (!stock || !counts || !base_count || !lookup_function || !component_function) {
        log_line("game functions not found exactly once (stock %d, counts %d, base count %d, lookup %d, component %d), "
            "mod NOT active", stock != NULL, counts != NULL, base_count != NULL, lookup_function != NULL,
            component_function != NULL);
        return;
    }
    uint8_t *build_tool_call = find_call(code, code_end, BUILD_TOOL_CALL_PATTERN, BUILD_TOOL_CALL_OFFSET, stock,
        "building tool stock");
    uint8_t *consume_call = find_call(code, code_end, CONSUME_CALL_PATTERN, CONSUME_CALL_OFFSET, stock,
        "consumption stock");
    uint8_t *queue_call = find_call(code, code_end, QUEUE_CALL_PATTERN, QUEUE_CALL_OFFSET, stock, "queue fill stock");
    uint8_t *counts_call = find_call(code, code_end, COUNTS_CALL_PATTERN, COUNTS_CALL_OFFSET, counts, "menu counts");
    if (!build_tool_call || !consume_call || !queue_call || !counts_call) return;
    uint8_t *matches[BASE_COUNT_CALLS];
    int base_count_matches = find_all(code, code_end, BASE_COUNT_CALL_PATTERN, matches, BASE_COUNT_CALLS);
    if (base_count_matches != BASE_COUNT_CALLS) {
        log_line("build check calls found %d times instead of %d, mod NOT active", base_count_matches,
            BASE_COUNT_CALLS);
        return;
    }
    for (int i = 0; i < BASE_COUNT_CALLS; i++) {
        if (call_target(matches[i] + BASE_COUNT_CALL_OFFSET) != base_count) {
            log_line("build check call %d calls something else, mod NOT active", i);
            return;
        }
    }

    uint8_t *order_jump = NULL, *predict_call = NULL;
    int inventory_first = find_inventory_first(code, code_end, &order_jump, &predict_call);

    uint8_t *stubs = alloc_near(image, nt->OptionalHeader.SizeOfImage);
    if (!stubs) {
        log_line("no memory near the game image, mod NOT active");
        return;
    }
    original_stock = (StockFn)stock;
    original_counts = (CountsFn)counts;
    original_base_count = (BaseCountFn)base_count;
    lookup = (LookupFn)lookup_function;
    get_component = (ComponentFn)component_function;
    storage_offset = *(uint32_t *)(component_function + COMPONENT_STORAGE_OFFSET_AT);
    write_jump(stubs, (void *)stock_wrapper);
    write_jump(stubs + 16, (void *)counts_wrapper);
    write_jump(stubs + 32, (void *)base_count_wrapper);
    write_consume_stub(stubs + 64);
    write_jump(stubs + 128, (void *)predict_wrapper);
    if (!seal_stubs(stubs)) {
        log_line("stub memory could not be made executable, mod NOT active");
        return;
    }
    int patched = redirect_call(build_tool_call, stubs) + redirect_call(consume_call, stubs + 64)
        + redirect_call(queue_call, stubs + 64) + redirect_call(counts_call, stubs + 16);
    for (int i = 0; i < BASE_COUNT_CALLS; i++) patched += redirect_call(matches[i] + BASE_COUNT_CALL_OFFSET, stubs + 32);
    if (patched != 4 + BASE_COUNT_CALLS) {
        log_line("patching failed, mod may be partly active");
        return;
    }
    log_line("mod active (stock rva %llx, menu counts rva %llx, build check rva %llx)",
        (unsigned long long)(stock - image), (unsigned long long)(counts - image),
        (unsigned long long)(base_count - image));
    if (inventory_first) {
        // The prediction first: without the order change, an empty prediction only delays the menu update.
        if (redirect_call(predict_call, stubs + 128) && remove_order_jump(order_jump)) log_line("inventory first active");
        else log_line("inventory first: patching failed, chests may still be used first when building");
    }
}

BOOL WINAPI DllMain(HINSTANCE instance, DWORD reason, LPVOID reserved)
{
    (void)reserved;
    if (reason == DLL_PROCESS_ATTACH) {
        DisableThreadLibraryCalls(instance);
        // The log lives next to this DLL: cross-base-storage.log.
        DWORD length = GetModuleFileNameA(instance, log_path, MAX_PATH);
        char *slash = (length > 0 && length < MAX_PATH) ? strrchr(log_path, '\\') : NULL;
        if (slash) {
            strcpy_s(settings_path, MAX_PATH, log_path);
            strcpy_s(settings_path + (slash + 1 - log_path), MAX_PATH - (slash + 1 - log_path), "cross-base-storage.ini");
            strcpy_s(slash + 1, MAX_PATH - (slash + 1 - log_path), "cross-base-storage.log");
        } else {
            log_path[0] = 0;
        }
        install();
    }
    return TRUE;
}
