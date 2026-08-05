// Key Definitions to know:
// MEM_RESERVE: carves out VA space without physical backing
// MEM_COMMIT: guarantees physical backing will exist if wanted to access
// MapUserPhysicalPages: installs/removes a VA leading to physical page mapping?
// AllocateUserPhysicalPages: OS gives a pool of physical pages, specifially its PFN

// DEBUGGER CHEATSHEET
// Left 8 digit number = start address, right = end address

// ? = equation/quick math
// lm = list module (lists all modules and where they are)
// ln = list near (give any address, and it will give which variable it belongs to)
// r = registers (shows all the cpu registers and the values within those registers and their variables/locations/instruction)
// kn = stack trace (current stack pointer (at top of VA space), return address (location of var it's returning to), and the function it is in (called call site))
// dv = dump variables (will show locations and values of all variables local to the function)
// bp = breakpoint (set breakpoint at function. ex: bp vmTest!main)
// g = go until breakpoint, completion, or crash
// u = unasemble (tells all the future instructions in your function)
// bl = lists all your breakpoints
// dd = Tries to show you the contents at an address
// .f+ go to next frame (who was the function before me?)
// dq = dump quad (dumps the values of 8 byte chunks at address specified)
// .logopen (opens text file with all the output of the debugger)
// .logclose
// gh = go ahead (degugger don't worry just continue)
// sxd av = (av = access violation) (tells debugger to stop breaking on this particular exception)
// !vprot = (tells you if its legit and gives you the address for the memory allocation and the state ex. MEM_RESERVE, etc.)
// q = quit process
// bd = remove breakpoint (ex. bd 1 removes breakpoint 1)
// ?? var_name = gives value of that variable
// x = see list of globals
// ba w8 address

// Common debugger error codes
// c0000005: access violation, touching memory you don't own

// Performance trace cheat sheet
// xperf -on base -stackwalk profile
// Then run your program
// xperf -stop -d trace1.etl
// trace1.etl
// Once in the trace, click Trace and then Load Symbols

// Pointer Cheat Sheet
// *(physical_page_to_virtual + i) == physical_page_to_virtual[i]

// Virtual Memory Management Concepts
// Get the VA: VA BASE + i * 4K (size of a page)
// Efficiency: unmapping takes more energy than mapping; therefore, batching allows optimization

// Disk states (may disk retrieval) (keep a free list for this)
// 1. Valid
// 2. Not valid (don't read) (soft fault afterwards)

// Four States of PTE (pfn, d, t, v)
// 1. Zero: (0,0,0,0)
// 2. Valid: (pfn,0,0,1)
// 3. Transition: (pfn, 0, 1, 0) In which the PFN can go from modified to standby
// also, in substate: possibility that the page is written to disk when switching PFN states
// a. can go soft fault
// b. can go to disk
// 4. Disk format (only in disk!) (disk index,1,0,0): page is repurposed from standby list

// States in PFN Metadata/Three type of list (optional)
// 1. free list: free page
// 2. active list (not mL compatible)
// 3. been modified by user list: pages that need to be written to disk
// Still transition state in pte; it means that v = 0, t = 1 but only stored in pages
// 4. standby list: list of pages already in disk
// Still transititon state in pte; it means that v = 0, t = 1 but stored in both pages and disk
// Length of list: invaluable information regarding trimming amount, batching, etc...

// createThread(function_to_begin_life_in): how to create other "coworkers"
    // when function_to_begin_life_in hits a return, thread dies (only when everything is done)
// waitForSingleObject
// waitForMultipleObject
// setEvent: based on certain criteria make, can trigger the thread to do stuff
    // i.e. trim pages when free pages num are low
// enterCriticalSection: serves as a lock to prevent race condition. only allows one thread to go at a time
    // and then call leave, allowing next thread
    // (when one thing is faster,
    // causing misinformation/when 2 cpus are accessing the same data, and one is going to be faster)
// leaveCriticalSection

// When do we want a lock?
// Given a chunk of memory, at least one thread is writing to it while the other is also either reading/writing
    // a) both threads are editing the same data concurrently
// Deadlock: when two threads have opposite order, such that one thread wants the lock of the other
// and the other thread wants the lock of the opposite (i.e. one thread: A -> B, second thread B -> A)
// as a thread only releases the locks after their entire task is complete (i.e. going from function A -> B -> DONE)
// solution:
        // a) if x amount of time passed and the task is not complete, release the locks
        // b) only allow one order (i.e. A -> B)
    // goal: detect it soon enough, so you can back up and avoid

// page table -> faults

#include <stdio.h>
#include <stdlib.h>
#include <windows.h>

// SIZE
#define PAGE_SIZE                   4096
#define KB(x)                       ((x) * 1024)
// Converts megabytes to bytes
#define MB(x)                       (((ULONG64)(x)) * 1024 * 1024)
#define GB(x)                       (MB(x) * 1024)

// Physical memory and disc sizing
#define NUMBER_OF_PHYSICAL_PAGES (GB(1) / PAGE_SIZE) // ((VIRTUAL_ADDRESS_SIZE / PAGE_SIZE) / 64)
#define NUM_DISC_PAGES (NUMBER_OF_PHYSICAL_PAGES) // (MB(2) / PAGE_SIZE)
#define MAX_DISC_PTE_BITS 40
#define MAX_DISC_SIZE ((ULONG64) 1 << MAX_DISC_PTE_BITS)

// Thread
#define NUM_THREADS 5

// Page table sectioning and aging
# define NUM_PTE_SECTIONS 64
#define NUM_AGES 8
#define CHUNKS_PER_PAGE (PAGE_SIZE / sizeof(ULONG_PTR))

// Triming and Writing Batch Size and Threseholds
// The bar we have to meet to trigger trimming measured by free+standby count
#define TRIM_LOW_BAR 500
#define EMERGENCY_LOW_BAR 120
#define TRIM_BATCH_SIZE 800 // How many pages to trim per batch
// TODO: later make this number dynamic based on pressure instead of constat
#define MINIMUM_TRIM_SIZE (TRIM_BATCH_SIZE / 2)
// Number of modified pages we want before writing to disk
// Number can't exceed the disk size
# define WRITE_BATCH_SIZE 500

// Background Thread
#define AGE_TICK_MS 250 // How often the age function preemptively checks
#define CONSUMPTION_TICK 3 // How often the consumption thread preemptively checks

// Consumption
#define HISTORY_LENGTH 8
#define MIN_TICK_THRESHOLD 50

// Constants for the linear and random accesses
#define MIN_RUN_PAGES 16 // Shortest linear walk
#define MAX_RUN_PAGES 512 // Longest linear walk
#define REVISIT_CHANCE 4 // 1/REVISIT_CHANCE probability to revisit a spot
#define HOT_SPOTS 8

// NEW CONSTANTS: REORGANIZE
// How many sections we age per call
#define AGE_MIN_SECTIONS 4 // Minimum section
#define AGE_MAX_SECTIONS 20 // Max sections to age at a time
#define STANDBY_REFILL_BATCH 32 // Number of extra standby pages to repurpose during a get_free_pages call to add onto free list

// Global to keep track of the dynamic batch size of the ager
volatile LONG age_batch_sections = 10;

// Multiple VA support
#define SUPPORT_MULTIPLE_VA_TO_SAME_PAGE 1
#pragma comment(lib, "advapi32.lib")

// If multiple VA, it links to onecore.lib
#if SUPPORT_MULTIPLE_VA_TO_SAME_PAGE
#pragma comment(lib, "onecore.lib")
#endif

// Debug Assert
#define DEBUG 0

#if DEBUG
#define ASSERT(x) {if(!(x)) DebugBreak();}
#define DEBUG_PRINT(...) printf(__VA_ARGS__)
#else
#define ASSERT(x)
#define DEBUG_PRINT(...)
#endif

#define DIAGNOSTICS 0
#if DIAGNOSTICS
  #define DIAG_QPC(x)          QueryPerformanceCounter(x)
  #define DIAG_ADD64(dst, val) InterlockedAdd64(&(dst), (val))
  #define DIAG_INC(dst)        InterlockedIncrement(&(dst))
  #define DIAG_DECL(decl)      decl
#else
  #define DIAG_QPC(x)          ((void)0)
  #define DIAG_ADD64(dst, val) ((void)0)
  #define DIAG_INC(dst)        ((void)0)
  #define DIAG_DECL(decl)
#endif

#if DIAGNOSTICS
volatile LONG64 trim_total_qpc = 0;
volatile LONG   trim_call_count = 0;
volatile LONG64 trim_total_pages = 0;
volatile LONG64 write_map_qpc = 0;
volatile LONG64 write_memcpy_qpc = 0;
volatile LONG   write_call_count = 0;
volatile LONG64 write_total_pages = 0;
#endif

// MISCALLANOUS
#define TEMP_VA_SIZE 512 // Number of temp va slots for each thread
#define MINIMUM_WRITE_SIZE 200

// Number of tries a thread takes to attempt to obtain the locks of the three ndoes
// After that many fails, we go to the hard route
#define RW_LIST_MAX_FAILS 30
// Amount of time you wait after each retry after trying to obtain a lock, so we give time for other threads to finish
#define RW_LIST_MAX_WAITING_PERIOD 64

// List Primitives
// List Head Primitives to build Linked Lists
// Functions include: initialize, isListEmpty, insert, remove
// typedef struct _LIST_ENTRY {
//     struct _LIST_ENTRY *Flink;
//     struct _LIST_ENTRY *Blink;
// } LIST_ENTRY, *PLIST_ENTRY;

// STRUCT
// PTE types
typedef struct {
    ULONG_PTR valid: 1;
    ULONG_PTR frame_number: 40;
    ULONG_PTR age: 3;
    ULONG_PTR access: 1;
    ULONG_PTR reserved: 19;
} VALID_PTE, *PVALID_PTE;

typedef struct {
    ULONG_PTR valid: 1; // Always 0
    ULONG_PTR transition: 1; // Always 1
    ULONG_PTR frame_number: 40;
    ULONG_PTR reserved: 22;
} TRANSITION_PTE, *PTRANSITION_PTE;

typedef struct {
    ULONG_PTR valid: 1; // Always 0
    ULONG_PTR transition: 1; // Always 1
    ULONG_PTR disc: 1;
    ULONG_PTR disc_index: MAX_DISC_PTE_BITS;
    ULONG_PTR reserved: 64 - MAX_DISC_PTE_BITS - 3;
} DISC_PTE, *PDISC_PTE;

typedef struct {
    union {
        VALID_PTE hardware;
        TRANSITION_PTE transition;
        DISC_PTE disc;
        // Include a pointer to the entire PTE;
        ULONG_PTR entire;
    };
} PTE, *PPTE;

// List Header
// Define the List Header Struct
typedef struct {
    LIST_ENTRY entry;
    CRITICAL_SECTION lock;
    ULONG_PTR size;
} LIST_HEAD, * PLIST_HEAD;

// RW locked list: allows multiple threads to touch a list at a time, reducing lock contention
// For shared lock: a thread needs to get the pfn individual lock of 3 nodes the node itself, the flink, and its blink
// For exclusive lock: wait for all threads to let go of their shared lock, and then grab the head lock list
typedef struct RW_LIST_HEAD {
    LIST_ENTRY entry;
    volatile LONG64 size;
    // Allows two routes: fast (shared lock) and slow (exclusive lock)
    SRWLOCK rw_lock;
    CRITICAL_SECTION head_lock;
} RW_LIST_HEAD, *PRW_LIST_HEAD;

// Custom struct for our PFNs
typedef struct {
    LIST_ENTRY list;
    PPTE pte;
    ULONG_PTR disc_index; // MAX_DISC_PTE_BITS;
    // Had to make these ULONG by themselves as previously they shared the same physical space
    // As a result, the read write between two threads resulted in stale data
    ULONG_PTR isOccupied; // 2 bits// 00-free, 01-active, 10-modified, 11-standby
    ULONG_PTR write_in_progress; // 1 bits; // 1: being written to disc
    CRITICAL_SECTION lock;
} pfn_metadata;

typedef struct {
    CRITICAL_SECTION lock;
    LIST_ENTRY age_lists[NUM_AGES];
    ULONG_PTR age_counts[NUM_AGES];
} PTE_SECTION;

typedef struct _DISC_SLOT_ENTRY {
    LIST_ENTRY list;
    ULONG_PTR  slot_index;
    BOOLEAN valid;
} DISC_SLOT_ENTRY, *PDISC_SLOT_ENTRY;

// Borrowed Noah's random function
typedef struct {
    ULONG_PTR state;
    ULONG_PTR counter;
} THREAD_RNG_STATE;

// Struct for the number of different faults per thread
typedef struct {
    int thread_id;
    int hard_faults;
    int soft_faults;
    int repurpose_faults;
    int total_accesses;
    double elapsed_ms;
} ThreadStats;

// Per-thread state for the locality access pattern.
typedef struct {
    ULONG_PTR base_page;        // Start of the current linear run
    ULONG_PTR cur_page;         // Where we are within the run
    ULONG_PTR run_left;         // Pages remaining before a new jump
    ULONG_PTR hot[HOT_SPOTS];   // Ring of recently-visited bases
    int       hot_next;         // Next slot to overwrite
    int       hot_count;        // How many ring slots are filled
} LOCALITY_STATE;

// Handles
HANDLE shutdown_event;
HANDLE trim_needed;
HANDLE write_needed;
HANDLE age_needed;
HANDLE pages_available;

// Lock
CRITICAL_SECTION disc_lock;

// Check if the threads are active
volatile LONG write_thread_active = 0;
volatile LONG trim_thread_active = 0;

// Virtual Address space
// Pointer to the start of the space allocated for the user virtual address
PULONG_PTR start;
// Pointer to the start of the space allocated for the system virtual address
// Used specifically for mapping page's data to the disk
PULONG_PTR system_va_start;
ULONG_PTR virtual_address_size_in_unsigned_chunks;
// Pointer to the start of the space allocated for the faulting thread to temporarily transfer their data to create a page
PULONG_PTR fault_va_start;

// Physical frames
// Sets up global variable that points to the allocated memory of the pfn_metadata
pfn_metadata * pfn_table;
// Sets up global variable that stores the value of the biggest frame number magnitude
ULONG_PTR max_frame_number = 0;
PULONG_PTR physical_page_numbers;
// Number of physical pages that are actually allocated by the OS
ULONG_PTR physical_page_count = NUMBER_OF_PHYSICAL_PAGES;

// TODO: update the repurpose fault counters
// Global Lists to keep track of the PFNs in each state
LIST_HEAD pfn_modified_list;
RW_LIST_HEAD pfn_standby_list;

// Disc
// Initialize disc
PVOID official_disc;
ULONG_PTR disc_page_count = NUM_DISC_PAGES;
int filled_disc_slots = 0;
// Pointer to the memory resevered for the disk
DISC_SLOT_ENTRY * disc_slot_entry;
// Free list of disc slots
LIST_ENTRY disc_free_list;

// NEW GLOBALS: REORGANIZE
// Keeps track of the age hand
ULONG_PTR age_hand = 0;

// Emergency vs preemptive trim tracking
volatile LONG emergency_trim_count = 0;   // worker ran dry, forced trim
volatile LONG preemptive_trim_count = 0;  // consumption_rate got ahead of it
volatile LONG stall_count = 0;            // worker actually blocked on pages_available
volatile LONG64 emergency_pages_trimmed = 0;
volatile LONG64 preemptive_pages_trimmed = 0;

// Flag set by the emergency path, read+cleared by trim_pages
volatile LONG trim_is_emergency = 0;

volatile ULONG last_avg_per_tick;

// Per-thread stats
// __declspec(thread) keyword: allows each thread to have it's own private copy of the below variables that act as globals
__declspec(thread) int my_hard_faults = 0;
__declspec(thread) int my_soft_faults = 0;
__declspec(thread) int my_repurpose_faults = 0;
__declspec(thread) int my_thread_id = -1;
// Each thread's current temp VA counter
__declspec(thread) int my_temp_va_count = 0; // How many slots currently using

// Create an array that demonstrates each thread's final results
ThreadStats final_results[NUM_THREADS];

// Consumption
volatile ULONG64 pages_consumed = 0;
ULONG_PTR history[HISTORY_LENGTH] = {0};
int history_index = 0;
ULONG_PTR last_pages_consumed = 0;

// Create a global variable of an array of PTEs
PPTE page_table;

// Pointer to the array of pte_sections
PTE_SECTION *pte_sections;
// Number of PTEs per section
ULONG_PTR ptes_per_section;

// Array of free list heads
LIST_HEAD free_lists[NUM_THREADS];
volatile LONG64 total_free_pages = 0; // Estimate to avialble free pages
volatile LONG free_list_hand = 0;

// Forward function declarartion
VOID trim_pages(void);

VOID
InitializeListHead (
    PLIST_ENTRY ListHead
    )
{
    ListHead->Flink = ListHead->Blink = ListHead;

    return;
}

BOOLEAN
IsListEmpty (
    PLIST_ENTRY ListHead
    )
{
    return (BOOLEAN) (ListHead->Flink == ListHead);
}

VOID
InsertTailList (
    PLIST_ENTRY ListHead,
    PLIST_ENTRY Entry
    )
{
    PLIST_ENTRY Blink;

    //
    // Insert a new entry at the tail.
    //

    Blink = ListHead->Blink;

    Entry->Flink = ListHead;
    Entry->Blink = Blink;

    Blink->Flink = Entry;

    ListHead->Blink = Entry;

    return;
}

PLIST_ENTRY
RemoveHeadList (
    PLIST_ENTRY ListHead
    )
{
    // TODO: batch remove head list for standby list, and repurpose several and put on free list
    PLIST_ENTRY Flink;
    PLIST_ENTRY Entry;

    // TODO: parallelized acccess
    // TODO: lock order: lock me, lock flink, lock blink
    // TODO: if you are on the first, you have to lock head
    // TODO: SRW LOCK
    // TODO: during normal, do SRWLock
    // TODO: people come in shared, try to do it the nice way X amount of times, and once you fail
    // TODO: when they deadlock, they then let go, chane to get lock exclusive
    // TODO: when count goes down to 0

    // TODO: batch, try to do unlock for most, and then do that and do either MINIMUM or try
    // TODO: do a bit map for the disc
    // Remove the entry currently at the head of the list.
    //

    Entry = ListHead->Flink;
    Flink = Entry->Flink;
    ListHead->Flink = Flink;
    Flink->Blink = ListHead;

    return Entry;
}

BOOLEAN
RemoveEntryList (
    PLIST_ENTRY Entry
    )
{
    PLIST_ENTRY Blink;
    PLIST_ENTRY Flink;

    //
    // Remove the caller's known entry.
    //

    Flink = Entry->Flink;
    Blink = Entry->Blink;
    Blink->Flink = Flink;
    Flink->Blink = Blink;

    //
    // Return whether list is now empty.
    //
    return (BOOLEAN) (Flink == Blink);
}

VOID
initialize_list_head(PLIST_HEAD head) {
    InitializeListHead(&head->entry);
    InitializeCriticalSectionAndSpinCount(&head->lock, 0x00FFFFFF);
    head->size = 0;

    return;
}

// RW List Helpers
// Function that returns the individual lock that guards either the list head or the page
// Helps streamline the logic in the edge cases when we are trying to lock the blink, which happens to be the head
CRITICAL_SECTION*
    get_node_lock(PRW_LIST_HEAD list, PLIST_ENTRY entry) {
    // If its the list head, return the head_lock from the RW list struct
    if (entry == &list->entry) {
        return &list->head_lock;
    }

    // If its a page, return the individual lock per pfn
    // Cast the list_entry to pfn_metadata to get the page's lock
    pfn_metadata * pfn = (pfn_metadata*) entry;
    return &pfn->lock;
}

// Controls the waiting period after failing to grap a list_entry lock in a RW list
// Pause to let other threads finish and release the lock
// Idea is for the waiting period increase after the number of fails
VOID
failed_rw_waiting_period(ULONG_PTR num_fails) {
    // Initial rate is 1:1
    ULONG_PTR spin = num_fails;

    // Ensure we don't wait forever, so cap it
    if (spin > RW_LIST_MAX_WAITING_PERIOD) {
        spin = RW_LIST_MAX_WAITING_PERIOD;
    }

    // Enact the waiting period by calling Yield Processor
    // Yield Processor: CPU instruction that lets a thread spin in place waiting, without full power. Waits without going to sleep
    for (ULONG_PTR i = 0; i < spin; i++) {
        YieldProcessor();
    }
}

// Initializes the RW Locked list
VOID
initialize_RW_list(PRW_LIST_HEAD list) {
    // Ensure the list is empty with Flink Blink poiting to itself
    InitializeListHead(&list->entry);
    list->size = 0;

    // Initialize shared/exlcusive lock and the head lock
    InitializeSRWLock(&list->rw_lock);
    InitializeCriticalSectionAndSpinCount(&list->head_lock, 0x00FFFFFF);
}

// Helper function to insert the tail at the end of a RW list
// Need to edit two fieds: the head_lock and the current_tail's pfn lock
// Two ways: optimized and then slow, after X amount of TryEnter
VOID
insert_tail_RW_list(PRW_LIST_HEAD list, pfn_metadata * pfn) {
    // Enter the optimized route by grabbing the shared lock
    AcquireSRWLockShared(&list->rw_lock);

    // Keep track of number of fails that will dictate when we leave and how long our waiting period is
    ULONG_PTR num_fails = 0;

    while (TRUE) {
        // Try to grab the head_lock since we need to change its Flink
        if (TryEnterCriticalSection(&list->head_lock) == FALSE) {
            num_fails++;

            // Continue to wait, and then try again until we tried enough times
            if (num_fails < RW_LIST_MAX_FAILS) {
                failed_rw_waiting_period(num_fails);
                continue;
            }

            // Failed too many times, try to go exclusive route
            break;
        }

        // Find the current tail and get its lock
        PLIST_ENTRY tail = list->entry.Blink;
        CRITICAL_SECTION * tail_lock = get_node_lock(list, tail);

        // Edge case: if the list is empty, the tail is equivalent to the head lock given the logic of get_node_lock
        // Double check
        BOOLEAN same_lock = FALSE;
        if (tail_lock == &list->head_lock) {
            same_lock = TRUE;
        }

        // If the list is not empty, need to get the tail_lock
        if (same_lock == FALSE) {
            if (TryEnterCriticalSection(tail_lock) == FALSE) {
                // If we couldn't get the tail lock, release all your current locks, and wait, hoping for the other threads to finish
                LeaveCriticalSection(&list->head_lock);

                num_fails++;

                if (num_fails < RW_LIST_MAX_FAILS) {
                    failed_rw_waiting_period(num_fails);
                    continue;
                }

                break;
            }
        }

        // Successfully obtained all the locks
        // Add the new pfn to the tail
        InsertTailList(&list->entry, &pfn->list);
        InterlockedIncrement64(&list->size);

        // Release the locks
        if (same_lock == FALSE) {
            LeaveCriticalSection(tail_lock);
        }

        // Release the head_lock
        LeaveCriticalSection(&list->head_lock);
        ReleaseSRWLockShared(&list->rw_lock);
        return;
    }

    // Slower route after breaking out of the while after failing too many times
    // Try to make the list Exclusive, so we are the only thread
    ReleaseSRWLockShared(&list->rw_lock);
    AcquireSRWLockExclusive(&list->rw_lock);

    InsertTailList(&list->entry, &pfn->list);
    InterlockedIncrement64(&list->size);

    ReleaseSRWLockExclusive(&list->rw_lock);

}

// Helper to remove a page off a RW list
// When called, caller holds its own page's lock
// Need to change its surronding Node's flink blink
VOID
remove_entry_RW_list(PRW_LIST_HEAD list, pfn_metadata * pfn) {
    // Try the optimized route
    AcquireSRWLockShared(&list->rw_lock);

    // Counter to keep track of how many times we fail to get the lock, caculating the waiting period and when we give up
    ULONG_PTR num_fails = 0;

    while (TRUE) {
        // Grab its linked list field
        PLIST_ENTRY entry = &pfn->list;

        // Check if its already unlinked, meaning another thread softfaulted on this entry
        if (entry->Flink == NULL) {
            ReleaseSRWLockShared(&list->rw_lock);
            return;
        }

        // Try to obtain the neighbors locks, starting with the Flink
        PLIST_ENTRY flink = entry->Flink;
        PLIST_ENTRY blink = entry->Blink;
        CRITICAL_SECTION * flink_lock = get_node_lock(list, flink);
        CRITICAL_SECTION * blink_lock = get_node_lock(list, blink);

        // Try to grab the flink's lock
        if (TryEnterCriticalSection(flink_lock) == FALSE) {
            num_fails++;

            // Go to the waiting period
            if (num_fails < RW_LIST_MAX_FAILS) {
                failed_rw_waiting_period(num_fails);
                continue;
            }

            // Missed too many times, move on
            break;
        }

        // Edge case: if the flink blink are the same entry, so a singular lock
        BOOLEAN same_lock = FALSE;
        if (blink_lock == flink_lock) {
            same_lock = TRUE;
        }

        // Try to grab the blink's node, if its different
        if (same_lock == FALSE) {
            if (TryEnterCriticalSection(blink_lock) == FALSE) {
                // If you failed to obatin the blink's lock, release the flink's lock, hoping that another thread will finish
                LeaveCriticalSection(flink_lock);
                num_fails++;

                if (num_fails < RW_LIST_MAX_FAILS) {
                    failed_rw_waiting_period(num_fails);
                    continue;
                }
                break;
            }
        }

        // Successfully obtained both the entry that we want to remove, and the locks of its flink and blink
        // Change the flink blink of the page's flink blink
        blink->Flink = flink;
        flink->Blink = blink;
        // Mark the current page off a list so get rid of its Flink Blink entries
        entry->Flink = NULL;
        entry->Blink = NULL;
        InterlockedDecrement64(&list->size);

        // Release the flink blink locks
        if (same_lock == FALSE) {
            LeaveCriticalSection(blink_lock);
        }
        LeaveCriticalSection(flink_lock);
        ReleaseSRWLockShared(&list->rw_lock);
        return;
    }

    // If we failed to try the fast way through the shared path, make it exclusive
    ReleaseSRWLockShared(&list->rw_lock);
    AcquireSRWLockExclusive(&list->rw_lock);

    // Check again if it is still linked in case another thread also soft faulted on this same page
    if (pfn->list.Flink != NULL) {
        RemoveEntryList(&pfn->list);
        pfn->list.Flink = NULL;
        pfn->list.Blink = NULL;
        InterlockedDecrement64(&list->size);
    }

    ReleaseSRWLockExclusive(&list->rw_lock);
}

// Helper that removes multiple pages off of a list fpr batching
// Need to detach that batch from the list: to do so we need to fix the head's flink and the page we stopped ats blink
// Try to get as many pages as possible, not guarntee batch_size
ULONG_PTR
get_batch_from_RW_list(PRW_LIST_HEAD list, pfn_metadata ** pfn_batch, ULONG_PTR max_batch_size) {
    // Make sure there is a reasonable batch size
    if (max_batch_size == 0) {
        return 0;
    }

    // Try the optimized route
    AcquireSRWLockShared(&list->rw_lock);

    // Keep track of failed lock acquires
    ULONG num_fails = 0;

    while (TRUE) {
        // Get the head_lock in order to get the front of the list
        if (TryEnterCriticalSection(&list->head_lock) == FALSE) {
            num_fails++;

            // Wait after a failed accquirement to let other threads hopefully finish
            if (num_fails < RW_LIST_MAX_FAILS) {
                failed_rw_waiting_period(num_fails);
                continue;
            }

            // Too many failures, try the hard way
            break;
        }

        // Currently hold the head lock
        ULONG_PTR curr_batch_size = 0;
        PLIST_ENTRY curr_page = list->entry.Flink;

        // We continue unless three things;
        // 1. Collected batch size
        // 2. List eneded, looped to the list_head again
        // 3. Hit a page we can't get the lock of

        // If we havent met the batch size or if we aren't at the end of the page
        while (curr_batch_size < max_batch_size && curr_page != &list->entry) {
            CRITICAL_SECTION * curr_lock = get_node_lock(list, curr_page);

            // If we can't lock the next page even though we haven't hit the batch size, just give up
            if (TryEnterCriticalSection(curr_lock) == FALSE) {
                break;
            }

            // If we managed to obtain the lock, add it to our batch and move on to the next page
            // Continue holding the page lock until the end
            pfn_batch[curr_batch_size] = (pfn_metadata *) curr_page;
            curr_batch_size++;
            curr_page = curr_page->Flink;
        }

        // If the list is empty or failed to get the first page
        if (curr_batch_size == 0) {
            // Release the boundary lock
            LeaveCriticalSection(&list->head_lock);
            ReleaseSRWLockShared(&list->rw_lock);
            return 0;
        }

        // Currently, curr_page is the first page we didnt get for three reasons
        // 1. curr_page is the head_lock so we looped through
        // 2. can't get curr_page's lock so move back by one, so boundary is a page we already hold (n-1)
        // 3. we can get curr_page lock
        // Check if it is case 1, which is we looped through the entire list
        BOOLEAN end_of_list = FALSE;
        if (curr_page == &list->entry) {
            end_of_list = TRUE;
        }
        // Tracks if we grabbed an extra lock
        BOOLEAN successful_boundary_lock = FALSE;

        // We want to get the lock
        // If curr_page is the head, we already have the lock
        // If curr_page is a page, we need to get the lock
        if (end_of_list == FALSE) {
            // Curr is a real page, so try to lock it
            if (TryEnterCriticalSection(get_node_lock(list, curr_page)) == TRUE) {
                successful_boundary_lock = TRUE;
            } else {
                // Failed to lock the page. Make the boundary, the last page we got so we already have the lock
                // Essentially shrink batch size by one
                curr_batch_size--;
                // Becomes new boundary
                curr_page = &pfn_batch[curr_batch_size]->list;

                // If we only obtained one page and couldn't get the second page's lock, and now the boundary is the
                // first page, there is no page on our batch list
                if (curr_batch_size == 0) {
                    LeaveCriticalSection(&pfn_batch[0]->lock);
                    LeaveCriticalSection(&list->head_lock);
                    ReleaseSRWLockShared(&list->rw_lock);
                    return 0;
                }

                successful_boundary_lock = TRUE;
            }
        }

        // Update the head to go straight to the boundary, which is now curr_page
        list->entry.Flink = curr_page;
        curr_page->Blink = &list->entry;

        // Update the pages that are going to get batched off and make sure that the flink blink is cleared
        for (ULONG_PTR i = 0; i < curr_batch_size; i++) {
            pfn_batch[i]->list.Flink = NULL;
            pfn_batch[i]->list.Blink = NULL;
        }
        // Update the list size counter
        InterlockedAdd64(&list->size, -(LONG64) curr_batch_size);

        // Release the boundary lock
        if (successful_boundary_lock == TRUE) {
            LeaveCriticalSection(get_node_lock(list, curr_page));
        }

        LeaveCriticalSection(&list->head_lock);
        ReleaseSRWLockShared(&list->rw_lock);

        return curr_batch_size;
    }

    // Slow path, making the RW shared list to exclusive
    ReleaseSRWLockShared(&list->rw_lock);
    AcquireSRWLockExclusive(&list->rw_lock);

    ULONG_PTR curr_batch_size = 0;
    PLIST_ENTRY curr_page = list->entry.Flink;

    while (curr_batch_size < max_batch_size && curr_page != &list->entry) {
        pfn_metadata * page = (pfn_metadata *) curr_page;

        // Only try to get the lock despite it being exclusive, since a soft faulter can grab a pfn per lock before us,
        // The soft faulter tries to get pfn-per-lock then list lock, so if we tried list lock to pfn-per-lock, deadlock
        if (TryEnterCriticalSection(get_node_lock(list, curr_page)) == FALSE) {
            break;
        }

        pfn_batch[curr_batch_size] = page;
        curr_batch_size++;
        curr_page = curr_page->Flink;
    }

    // Tracks if we grabbed an extra lock
    BOOLEAN successful_boundary_lock = FALSE;

    if (curr_batch_size > 0) {
        // Update the list data after removing the batch
        list->entry.Flink = curr_page;
        curr_page->Blink = &list->entry;

        // Clean up the pfn batches flink blink data to ensure that we don't walk to wrong pages
        for (ULONG_PTR i = 0; i < curr_batch_size; i++) {
            pfn_batch[i]->list.Flink = NULL;
            pfn_batch[i]->list.Blink = NULL;
        }
        InterlockedAdd64(&list->size, -(ULONG_PTR) curr_batch_size);
    }

    if (successful_boundary_lock == TRUE) {
        LeaveCriticalSection(get_node_lock(list, curr_page));
    }
    ReleaseSRWLockExclusive(&list->rw_lock);
    return curr_batch_size;
}


// Initializes the space for the page table and sets everything to valid bit, 0 and PFN, 0
PVOID
zero_malloc (SIZE_T num_bytes) {
    // Reserves a chunk of data
    PVOID pointer_to_PT = malloc (num_bytes);

    // If malloc fails, it breaks instead of continuing
    if (pointer_to_PT == NULL) {
        DebugBreak();
    }

    // Fills that region with zero bytes
    memset(pointer_to_PT, 0, num_bytes);
    return pointer_to_PT;
}

// PTE/VA HELPER
// Calculate the pointer to the specific PTE
PPTE
get_pte_from_va(PULONG_PTR arbitrary_va) {
    ULONG_PTR index = ((ULONG_PTR) arbitrary_va - (ULONG_PTR) start) / PAGE_SIZE;
    return page_table + index;
}

// Return the virtual address based on a pointer to it's corresponding pte
PULONG_PTR
get_va_from_pte(PPTE pte) {
    ULONG_PTR pte_index = pte - page_table;
    // Reverse the math for calculating the index in the get_pte_from_va
    return start + (pte_index * (PAGE_SIZE / sizeof (ULONG_PTR)));
}

// Return the PTE section the inputted pte belongs to
PTE_SECTION *
    get_section(PPTE pte) {
    ULONG_PTR index = (ULONG_PTR) (pte - page_table);
    return &pte_sections[index/ptes_per_section];
}


// Set the frame number to the physical page, set valid bit to 1, and update aging lists and counter
// Need to be called with a lock
VOID
set_pte_valid(PTE_SECTION * section, pfn_metadata * meta, PPTE pte, ULONG_PTR pfn) {
    // Build the new valid PTE, the valid PTE state for the InterlockedExchange
    PTE new;
    new.entire = 0;
    new.hardware.frame_number = pfn;
    new.hardware.age = 0;
    new.hardware.access = 0;
    new.hardware.valid = TRUE;

    // TODO: make sure compare was not the wrong choice, since i really don't know how to do compare
    // Currently, no one has access to this page. Its on the section lock and is not on a list so no thread has access concurrently.
    // No need for a compare especially because the old value could either be zero (hard fault) or transition (soft fault/disc)
    InterlockedExchange64((volatile LONG64 *) &pte->entire, (LONG64) new.entire);

    // Check that makes sure that any page inserted in two places at once (like age list and else where)
    // Helps check if our pfn data became stale, resulting in it going to the wrong path
    ASSERT(meta->list.Flink == NULL);
    // Add pte into the smallest, hottest age list (i.e. 0) in its corresponding section
    InsertTailList(&section->age_lists[0], &meta->list);
    // Update the PTE section's age count
    section->age_counts[0]++;
}

// Set the access bit during the worker thread to streamline the process without having to wait for pte section lock
VOID
set_access_bit(PPTE pte) {
    // Look at the current state of the pte
    PTE old;
    old.entire = pte->entire;

    // Confirm that it is a valid PTE. If not, it is likely a trimmer changed it's state; therefore, no need for access bit
    if (old.hardware.valid == 0) {
        return;
    }

    // If the access bit is already set, exit too
    if (old.hardware.access == 1) {
        return;
    }

    // Set the new PTE for the InterlockedCompareExchange
    PTE new;
    new.entire = old.entire;
    new.hardware.access = 1;

    // Update the PTE to its new value with access bit equal to 1 as long as it remains in its old state
    // Only try once because if it fails either the bit is already set or the page is no longer valid
    // Regardless if the set_access_bit succeds on setting the access bit, it will be ok; therefore, no need to put in a while(TRUE)
    // Only the aging and trimming thread can access
    // If the aging thread changed the state beforehand, the access bit is erased and access bit fails.
    // Trimming thread reads in the data, and marks it as invalid; therefore, the access bit fails
    ULONG_PTR actual_value = (ULONG_PTR) InterlockedCompareExchange64((volatile LONG64 *) &pte->entire, (LONG64) new.entire, (LONG64) old.entire);

    // Successfully updated its access bit
    if (actual_value == old.entire) {
        return;
    }
}

// Retursn whether or now the access bit was reset
// If return True, it should be aged
BOOLEAN
clear_access_bit(PPTE pte) {
    // Read the PTE's current state
    PTE old;
    old.entire = pte->entire;

    // If the PTE is invalid, there is no need for aging nor access bit
    if (old.hardware.valid == 0) {
        return FALSE;
    }

    // If the access bit was already invalid, no need to clear
    if (old.hardware.access == 0) {
        return FALSE;
    }

    // Set the new PTE all at once for Interlocked Compare Exchange
    PTE new;
    new.entire = old.entire;
    new.hardware.access = 0;

    // Write will always succeed
    // Given the function is currently holding the pte section lock, trimming thread cannot access
    // So, set_access_bit would not work as the access bit is already valid; therefore, clearing will always succeed
    ULONG_PTR actual_value = (ULONG_PTR) InterlockedCompareExchange64((volatile LONG64 *) &pte->entire, (LONG64) new.entire, (LONG64) old.entire);
    ASSERT(actual_value == old.entire);
    return TRUE;
}

// PFN TABLE HELPERS
VOID
setup_pfn_table(PULONG_PTR physical_page_numbers, ULONG_PTR physical_page_count) {
    // Find the maximum frame number by iterating through every physical page
    max_frame_number = 0;
    for (int i = 0; i < physical_page_count; i++) {
        if (max_frame_number < physical_page_numbers[i]) {
            max_frame_number = physical_page_numbers[i];
        }
    }

    // Reserve memory for our pfn_table
    ULONG_PTR pfn_table_size = (max_frame_number + 1) * sizeof(pfn_metadata);
    pfn_table = VirtualAlloc(NULL, pfn_table_size, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);

    // Check if the memory was successfully reserved
    ASSERT(pfn_table != NULL);

    // Commit the real physical pages by iterating through the physical page numbers and committing them
    for (int i = 0; i < physical_page_count; i++) {
        // Find the frame number
        ULONG_PTR frame_number = physical_page_numbers[i];
        // Find the address of the corresponding pfn_table entry of that frame_number
        pfn_metadata * pfn = &pfn_table[frame_number];

        memset(pfn, 0, sizeof(pfn_metadata));
        InitializeCriticalSectionAndSpinCount(&pfn->lock, 0x00000400);
        // Set up pfn per lock, single threaded no need for locks
        int index = (int)(frame_number % NUM_THREADS);
        InsertTailList(&free_lists[index].entry, &pfn->list);
        free_lists[index].size++;
        total_free_pages++;      }
}

pfn_metadata*
    find_pfn_from_frame_number(ULONG_PTR frame_number) {
    return &pfn_table[frame_number];
}
// Based on the address of a pfn_entry, find the frame number using pointer arithmetic
ULONG_PTR
find_frame_number_from_pfn(pfn_metadata *pfn) {
    ASSERT(pfn >= pfn_table);
    return (ULONG_PTR) (pfn - pfn_table);
}

// RNG and LOCALITY
// High-quality XOR shift generator
ULONG64 GetNextRandom(THREAD_RNG_STATE *rng) {
    ULONG64 x = rng->state;

    // High-quality XOR shift with good statistical properties
    x ^= x << 13;
    x ^= x >> 7;
    x ^= x << 17;

    rng->state = x;
    rng->counter++;

    // Occasionally reseed with fresh entropy
    if ((rng->counter & 0xFFFF) == 0) {
        x ^= __rdtsc(); // Mix in fresh entropy periodically
        rng->state = x;
    }

    return x;
}

// Initialize RNG state with non-deterministic seed
VOID InitializeThreadRNG(THREAD_RNG_STATE *rng) {
    LARGE_INTEGER perfCounter;
    ULONG64 rdtsc = __rdtsc();
    ULONG64 processId = GetCurrentProcessId();
    ULONG64 threadId = GetCurrentThreadId();

    QueryPerformanceCounter(&perfCounter);

    // Combine multiple entropy sources for non-deterministic seed
    rng->state = rdtsc ^ perfCounter.QuadPart ^
                 (processId << 32) ^ (threadId << 16) ^
                 ((ULONG64) rng << 8); // Use stack address as additional entropy

    // Ensure state is never zero (would break XOR shift)
    if (rng->state == 0) {
        rng->state = 0x123456789ABCDEF1ULL;
    }

    rng->counter = 0;

    // Warm up the generator to improve distribution
    for (int i = 0; i < 32; i++) {
        GetNextRandom(rng);
    }
}

VOID
init_locality_state(LOCALITY_STATE *loc, THREAD_RNG_STATE *rng, ULONG_PTR total_pages) {
    memset(loc, 0, sizeof(*loc));
    loc->base_page = GetNextRandom(rng) % total_pages;
    loc->cur_page  = loc->base_page;
    // 0 forces a fresh run on the first pick
    loc->run_left  = 0;
}

// Chooses the next VA using linear runs + hot-spot revisits.
// Advancing within a run is handled by the caller on a successful access
// (via locality_advance), so this only re-derives the VA each call.
PULONG_PTR
locality_next_va(LOCALITY_STATE *loc, THREAD_RNG_STATE *rng, ULONG_PTR total_pages) {

    if (loc->run_left == 0) {                       // start a fresh run
        ULONG_PTR r = GetNextRandom(rng);

        if (loc->hot_count > 0 && (r % REVISIT_CHANCE) == 0) {
            // Hot jump: back to a recent base (should still be young).
            loc->base_page = loc->hot[GetNextRandom(rng) % loc->hot_count];
        } else {
            // Cold jump: somewhere new, and record it as a hot spot.
            loc->base_page = GetNextRandom(rng) % total_pages;
            loc->hot[loc->hot_next] = loc->base_page;
            loc->hot_next = (loc->hot_next + 1) % HOT_SPOTS;
            if (loc->hot_count < HOT_SPOTS) loc->hot_count++;
        }

        loc->run_left = MIN_RUN_PAGES +
                        (GetNextRandom(rng) % (MAX_RUN_PAGES - MIN_RUN_PAGES));

        if (loc->base_page + loc->run_left > total_pages) {   // clamp to VA space
            loc->run_left = total_pages - loc->base_page;
            if (loc->run_left == 0) { loc->base_page = 0; loc->run_left = MIN_RUN_PAGES; }
        }

        loc->cur_page = loc->base_page;
    }

    return start + (loc->cur_page * CHUNKS_PER_PAGE);
}

// Call once after a successful (non-faulting) access to step through the run.
VOID
locality_advance(LOCALITY_STATE *loc) {
    loc->cur_page++;
    loc->run_left--;
}

// DISC HELPER
PVOID
create_page_file() {
    // Number_of_pages restricted by the disc size
    if (disc_page_count > MAX_DISC_SIZE) {
        disc_page_count = MAX_DISC_SIZE;
    }

    // In the memory, reserve pages for our fake disc
    ULONG_PTR num_bytes = disc_page_count * PAGE_SIZE;
    official_disc = malloc(num_bytes);

    // If the malloc fails, attempt to allocate half the memory size until it succeds
    while (official_disc == NULL) {
        num_bytes /= 2;
        official_disc = malloc (num_bytes);
    }

    disc_page_count = num_bytes / PAGE_SIZE;

    // Allocate memory for the disc slot entries
    disc_slot_entry = (DISC_SLOT_ENTRY *) zero_malloc (disc_page_count * sizeof(DISC_SLOT_ENTRY));

    // Build the free list of free disc pages
    InitializeListHead(&disc_free_list);

    // Iterate through the disc free list and initialize all flinks
    for (int i = 0; i < disc_page_count; i++) {
        DISC_SLOT_ENTRY *entry = &disc_slot_entry[i];
        // Update slot index which is apart of the disk struct
        entry->slot_index = i;
        entry->valid = FALSE;

        InsertTailList(&disc_free_list, &entry->list);
    }
    return official_disc;
}

// Iterate through the disc to find an empty page based on the disc metadata
int find_free_disc_slot() {
    EnterCriticalSection(&disc_lock);

    // Check if free list is empty, meaning disk is full
    if (IsListEmpty(&disc_free_list)) {
        printf("DISK find_free_disc_slot: disk full filled=%d / total=%llu\n",
            filled_disc_slots, (unsigned long long)disc_page_count);
        LeaveCriticalSection(&disc_lock);
        return -1;
    }

    // Pop the head of the disc free list making the operation O(1)
    PLIST_ENTRY list_entry = RemoveHeadList(&disc_free_list);

    // Find the slot index of the free disc page to return
    DISC_SLOT_ENTRY * slot_entry = (DISC_SLOT_ENTRY *) list_entry;
    ULONG_PTR slot = slot_entry->slot_index;

    // Update disc metadata
    slot_entry->valid = TRUE;

    // Update the occupied disc slot counter
    filled_disc_slots++;

    LeaveCriticalSection(&disc_lock);
    return slot;
}

// Free disc space
VOID
empty_disc_slot(ULONG_PTR slot) {

    if (slot >= disc_page_count) {
        printf("Disk slot out of bounds\n");
        DebugBreak();
        return;
    }

    EnterCriticalSection(&disc_lock);
    DISC_SLOT_ENTRY *entry = &disc_slot_entry[slot];

    if (entry->valid == FALSE) {
        printf("Disc metadata twice FALSE\n");
        DebugBreak();
        LeaveCriticalSection(&disc_lock);
        return;
    }

    // Update Disc Metadata and Counters
    entry->valid = FALSE;
    InsertTailList(&disc_free_list, &entry->list);
    filled_disc_slots--;

    LeaveCriticalSection(&disc_lock);
}

VOID
write_to_disk (int count, pfn_metadata ** pages_to_write, ULONG_PTR * disc_slots) {
    LARGE_INTEGER freq, t0, t1;
    QueryPerformanceFrequency(&freq);
    // Query Performance Frequency: tells how many ticks per second the performance counter useees
    // Query PerformanceCounter: gives the slapsed ticks

    // Array for mapping
    ULONG_PTR pfn_array[WRITE_BATCH_SIZE];
    for (int i = 0; i < count; i += 1) {
        // Get the pointer of the pfn metadata which is the same as the address of entry
        // As in pte metadata struct, entry is intentionally the first entry of pfn metadata
        pfn_metadata* meta = pages_to_write[i];
        // Get the frame number of physical page
        pfn_array[i] = find_frame_number_from_pfn(meta);
    }

    // Map multiple pages to sys VA at once for efficiency
    DIAG_QPC(&t0);
    if (MapUserPhysicalPages((PVOID) system_va_start, count, pfn_array) == FALSE) {
        printf("WRITE TO DISK: map failed, count=%d\n", count);
        DebugBreak();
        return;
    }
    DIAG_QPC(&t1);
    DIAG_ADD64(write_map_qpc, t1.QuadPart - t0.QuadPart);

    // Copy the data from the page to the disk
    DIAG_QPC(&t0);
    for (int i = 0; i < count; i += 1) {
        // Find the system slots va
        PULONG_PTR system_slot = (PULONG_PTR) ((PBYTE)system_va_start + i * PAGE_SIZE);

        // Default of the disc_slots will be -1
        disc_slots[i] = (ULONG_PTR) -1;
        // Find a free disc slot
        int disc_slot = find_free_disc_slot();

        if (disc_slot == -1) {
            printf("WRITE TO DISK: No free disc slot for page %d as its filled=%d / total=%llu\n",
                i, filled_disc_slots, (unsigned long long)disc_page_count);
            continue;
        }

        // Write the data to the disc
        memcpy((char*)official_disc + disc_slot * PAGE_SIZE, system_slot, PAGE_SIZE);

        // Save the free disc slot into an array
        disc_slots[i] = disc_slot;
    }
    DIAG_QPC(&t1);
    DIAG_ADD64(write_memcpy_qpc, t1.QuadPart - t0.QuadPart);

    DIAG_QPC(&t0);
    // Batch unmap: use mapUserPhysicalPages given all slots are contigous
    if (MapUserPhysicalPages((PVOID)system_va_start, count, NULL) == FALSE) {
        printf("WRITE TO DISK: batch unmap map failed, count=%d\n", count);
        DebugBreak();
    }
    DIAG_QPC(&t1);
    DIAG_ADD64(write_map_qpc, t1.QuadPart - t0.QuadPart);
}

static BOOL
need_to_write(void) {
    // Write to disk: if there are enough modified pages or if it is an emergency
    int modified = pfn_modified_list.size;
    int supply = total_free_pages + pfn_standby_list.size;

    // full batch, have enough of modified pages to write to disk
    if (modified >= WRITE_BATCH_SIZE) {
        return TRUE;
    }

    BOOL emergency = (supply < EMERGENCY_LOW_BAR);
    // Write when there is an emergency but also ensure that there is enough modified pages to make it worthwhile
    return emergency && modified >= MINIMUM_WRITE_SIZE;
}

// IN order, to get free list, we need more standby
VOID
request_pages(BOOL is_emergency) {
    // If I have pages worth writing: modified met full batch or emergency with some modified pages
    if (need_to_write()) {
        if (InterlockedCompareExchange(&write_thread_active, 1, 0) == 0) {
            SetEvent(write_needed);
        }
        // Continue, so we can also consider the need for trimming
    }

    // Record if this is an emergency, which helps the trimmer calculuate batch_size
    if (is_emergency) {
        InterlockedExchange(&trim_is_emergency, 1);
    }

    // If there are not enough modified pages to write, must trim
    if (InterlockedCompareExchange(&trim_thread_active, 1, 0) == 0) {
        SetEvent(trim_needed);
    }

}

// Get Free Pages Helpers
// Go through each free list to find a empty page
pfn_metadata*
    get_free_list_page (void) {
    for (int i = 0; i < NUM_THREADS; i++) {
        int index = (my_thread_id + i) % NUM_THREADS;
        LIST_HEAD *curr_free_list = &free_lists[index];

        // Move on if we can't obtain the lock
        if (TryEnterCriticalSection(&curr_free_list->lock) == FALSE) {
            continue;
        }

        // Check if it is empty
        if (IsListEmpty(&curr_free_list->entry)) {
            LeaveCriticalSection(&curr_free_list->lock);
            continue;
        }

        PLIST_ENTRY entry = RemoveHeadList(&curr_free_list->entry);
        curr_free_list->size--;

        // Decrement the total free pages estimate counter
        InterlockedDecrement64(&total_free_pages);

        pfn_metadata * meta = (pfn_metadata *) entry;
        meta->isOccupied = 1;

        meta->list.Flink = NULL;
        meta->list.Blink = NULL;
        LeaveCriticalSection(&curr_free_list->lock);
        return meta;
    }
    return NULL;
}
// Repurposes multiple pages: one reserved for the faulting thread and the rest added to the free list
pfn_metadata *
repurpose_standby(VOID) {
    pfn_metadata * pfn_batch[STANDBY_REFILL_BATCH];

    // Try to grab a full batch
    ULONG_PTR count = get_batch_from_RW_list(&pfn_standby_list, pfn_batch, STANDBY_REFILL_BATCH);

    if (count == 0) {
        return NULL;
    }

    // Flip each page's PTE from transition to disc format
    for (ULONG_PTR i = 0; i < count; i += 1) {
        pfn_metadata * meta = pfn_batch[i];
        PPTE old_pte = meta->pte;

        // Change the PTE from transition to disk format
        while (TRUE) {
            PTE old;
            old.entire = old_pte->entire;

            // Make the new PTE state
            // Mark as invalid, not transition, in disc, and change PFN to disk address
            PTE new;
            new.entire = old.entire;
            new.disc.valid = 0;
            new.disc.transition = 0;
            new.disc.disc = 1;
            new.disc.disc_index = meta->disc_index;

            // No need for a PTE lock because
            ULONG_PTR actual_value = (ULONG_PTR) InterlockedCompareExchange64(
                (volatile LONG64 *) &old_pte->entire,
                (LONG64) new.entire, (LONG64) old.entire);

            // Check if it successed
            if (actual_value == old.entire) {
                break;
            }
        }

        meta->isOccupied = 1;
    }

    // Keep the first page for the caller to use and unlock it given they came locked
    pfn_metadata * return_pfn = pfn_batch[0];
    LeaveCriticalSection(&return_pfn->lock);

    // Add the remaining newly repurposed pages onto the free list
    // They came locked so need to unlock it
    if (count > 1) {
        // Update the free list hands so pages are distributed evenly across the many free lists
        int index = (int) (InterlockedIncrement(&free_list_hand) % NUM_THREADS);
        LIST_HEAD * curr_free_list = &free_lists[index];

        EnterCriticalSection(&curr_free_list->lock);

        for (ULONG_PTR i = 1; i < count; i += 1) {
            InsertTailList(&curr_free_list->entry, &pfn_batch[i]->list);
            curr_free_list->size++;
            LeaveCriticalSection(&pfn_batch[i]->lock);
        }

        InterlockedAdd64(&total_free_pages, (LONG64) (count - 1));
        LeaveCriticalSection(&curr_free_list->lock);
    }

    return return_pfn;

}

// Page Helper
pfn_metadata *
get_free_page() {
    // Premptive trim
    if (total_free_pages + pfn_standby_list.size < TRIM_LOW_BAR) {
        request_pages(FALSE);
    }

    // Free: Check if the free list is empty without a lock, if so, continue
    // If not, verify status, and proceed to take a page
    // TODO: make it a read, interlocked
    if (total_free_pages > 0) {
        // Try to go through the many free lists to find a free page
        // Not guarnteed
        pfn_metadata * meta = get_free_list_page();

        // Successfully retrieved a page
        if (meta != NULL) {
            InterlockedIncrement64(&pages_consumed);
            return meta;
        }
    }

    // Standby: Repurpose the page from the standby list
    // Without a lock, check if the standby list has pages. If so, verify. Else, continue.
    // Allows us to reduce lock contention
    if (IsListEmpty(&pfn_standby_list.entry) == FALSE) {
        pfn_metadata * meta = repurpose_standby();

        if (meta != NULL) {
            InterlockedIncrement64(&pages_consumed);
            return meta;
        }
    }

    // Reset the event
    ResetEvent(pages_available);
    // Failed to get any pages on free or standby, must trim
    request_pages(TRUE);
    return NULL;
}

VOID
activate_page (PPTE pte, pfn_metadata *meta, ULONG_PTR pfn) {
    // Point physical page back to PTE
    meta->pte = pte;
    // Update pfn status
    meta->isOccupied = 1;
}

// MISCENLLANOUS FUNCTIONS: REORGANIZE
// Function that calculates the PTE based on the thread id given each thread has a set amount of allocated space
PULONG_PTR
get_temp_va(int k) {
    ULONG_PTR temp_start = (ULONG_PTR)my_thread_id * TEMP_VA_SIZE;
    return (PULONG_PTR)((PBYTE)fault_va_start + (temp_start + k) * PAGE_SIZE);
}


// Aging
VOID
age_pages() {
    int curr_num_section = (int) InterlockedCompareExchange(&age_batch_sections, 0, 0);

    for (int i = 0; i < curr_num_section; i++) {
        // Calculate current section
        int curr_section = (age_hand + i) % NUM_PTE_SECTIONS;
        PTE_SECTION * section = &pte_sections[curr_section];

        EnterCriticalSection(&section->lock);

        // Age from oldest to youngest skipping any pages that already reached the maximum age
        for (int age = NUM_AGES - 2; age >= 0; age--) {
            // Iterate through each page in the age bucket and age each PTE by one
            while (section->age_counts[age] > 0) {
                // Remove and store the pointer to the head of the current age linked list
                pfn_metadata * pfn = (pfn_metadata *) RemoveHeadList(&section->age_lists[age]);
                section->age_counts[age]--;

                // Get the pointer to the PTE to update its access bit if needed
                PPTE pte = pfn->pte;

                // Figure out which new list to put the page onto
                int new_age = -1;

                PTE old;
                old.entire = pte->entire;

                // Need to make sure the aging thread succeeds in updating its age
                // Therefore, its in a forever loop because it must fight the lock contention
                while (TRUE) {

                    // TODO: if never hit, remove. Instead chage to an ASSERT
                    // Check if the trimmer already got to it to prevent unnecessary calls
                    // And to get out of this forever loop
                    if (old.hardware.valid == 0) {
                        // Make an impossible age as it doesn't belong in one
                        new_age = -1;
                        DebugBreak();
                        break;
                    }

                    PTE new;
                    new.entire = old.entire;

                    // Based on the access bit, find its respecitive age bucket
                    // Its ok to read the PTE without a lock because the compare and exchange will catch any false claims
                    // Also, its the entire thing so nothing is halfwritten
                    if (old.hardware.access == 1) {
                        new.hardware.access = 0;
                        new.hardware.age = 0;
                        new_age = 0;
                    } else if (old.hardware.age < 7) {
                        // If it wasn't recently accessed, age like normal
                        new.hardware.age = age + 1;
                        new_age = age + 1;
                    } else {
                        new_age = 7;
                        // If the age bucket is 7, no need to udpate anything, break
                        break;
                    }

                    // If another thread changes the value of PTE, the edit will fail; therefore, we will read the updated value
                    // And make appropriate adjustments
                    ULONG_PTR actual_value =  (ULONG_PTR) InterlockedCompareExchange64((volatile LONG64 *) &pte->entire, (LONG64) new.entire, (LONG64) old.entire);

                    // If we successfully changed its value, leave
                    if (actual_value == old.entire) {
                        break;
                    }

                    old.entire = actual_value;

                }

                if (new_age >= 0) {
                    // Put it into the next age bucket (age + 1)
                    InsertTailList(&section->age_lists[new_age], &pfn->list);
                    section->age_counts[new_age]++;
                }
            }
        }

        LeaveCriticalSection(&section->lock);
    }
    // Store which section you left off at to ensure a fair aging function
    age_hand = (age_hand + curr_num_section) % NUM_PTE_SECTIONS;
}

// CONSUMPTION
// Must gurantee to at least trim one page, since every call means that trim was either triggered preemtpively or emergency
LONG
compute_trim_size(BOOL is_emergency) {
    // If its an emergency, worker is blocked right now; therefore, quickly trim some available pages
    if (is_emergency) {
        return MINIMUM_TRIM_SIZE;
    }

    // Prememptive trim batch size
    ULONG_PTR supply = (ULONG64)total_free_pages + pfn_standby_list.size;

    ULONG rate = InterlockedCompareExchange(&last_avg_per_tick, 0, 0);
    ULONG_PTR demand = rate * MIN_TICK_THRESHOLD;
    ULONG_PTR demand_gap;

    if (demand > supply) {
        // Number of pages needed based on the consumption rate
        demand_gap = demand - supply;
    } else {
        // Meeting the demand
        demand_gap = 0;
    }

    // Number of pages I lack right now
    LONG deficit = (LONG) (TRIM_LOW_BAR - supply);
    LONG trim_size;

    // Take the number that is bigger: deficit v. demand gap
    if (demand_gap > deficit) {
        trim_size = demand_gap;
    } else {
        trim_size = deficit;
    }

    // Set a ceiling
    if (trim_size > TRIM_BATCH_SIZE) {
        trim_size = TRIM_BATCH_SIZE;
    } else if (trim_size < 0) {
        trim_size = 0;
    }

    return trim_size;
}
VOID
consumption_rate() {
    // Read how many pages were consumed since last tick
    ULONG_PTR curr_pages_consumed = pages_consumed - last_pages_consumed;
    // Reset
    last_pages_consumed = pages_consumed;

    // Store into my history array
    history[history_index] = curr_pages_consumed;
    history_index = (history_index + 1) % HISTORY_LENGTH;

    ULONG_PTR total = 0;
    // Add up the total pages consumed over the past 16 ticks
    for (int i = 0; i < HISTORY_LENGTH; i++) {
        // Add up all the values of pages consumed
        total += history[i];
    }
    // Average pages consumed per tick during this window of time
    ULONG_PTR avg_per_tick = total / HISTORY_LENGTH;
    InterlockedExchange(&last_avg_per_tick, avg_per_tick);

    // Find batch size for aging but use the constant as a baseline
    int age_batch_size = AGE_MIN_SECTIONS;

    // CONSUMPTION_TICK is 3ms, so pages/sec = avg_per_tick * (1000/3)
    DEBUG_PRINT("[CONSUME] %llu pages/tick (~%llu pages/sec) | free=%llu standby=%llu modified=%llu\n",
        (unsigned long long)avg_per_tick,
        (unsigned long long)(avg_per_tick * 1000 / CONSUMPTION_TICK),
        (ULONG64)total_free_pages,
        (unsigned long long)pfn_standby_list.size,
        (unsigned long long)pfn_modified_list.size);
    DEBUG_PRINT("[FREE DIST] free1=%llu free2=%llu free3=%llu free4=%llu free5=%llu total=%lld\n",
        (unsigned long long)free_lists[0].size,
        (unsigned long long)free_lists[1].size,
        (unsigned long long)free_lists[2].size,
        (unsigned long long)free_lists[3].size,
        (unsigned long long)free_lists[4].size,
        (LONG64)total_free_pages);

    if (avg_per_tick > 0) {
        // At this rate, how many ticks until we run out of available pages without trimming/writing to disk
        ULONG_PTR remaining_ticks = ((ULONG64) total_free_pages + pfn_standby_list.size) / avg_per_tick;

        // Caclulate the dynamic batch size for the aging thread
        // For emergencies, do max aging sections
        if (remaining_ticks == 0) {
            age_batch_size = AGE_MAX_SECTIONS;
        } else {
            // Inverse relationships: greater the remaining_ticks, smaller the batch
            age_batch_size = (int)((NUM_PTE_SECTIONS * MIN_TICK_THRESHOLD) / remaining_ticks);
        }

        // If the age batch size is smaller the minimum baseline reset it
        if (age_batch_size < AGE_MIN_SECTIONS) {
            age_batch_size = AGE_MIN_SECTIONS;
        }
        if (age_batch_size > AGE_MAX_SECTIONS) {
            age_batch_size = AGE_MAX_SECTIONS;
        }

        // Use Interlocked operations to ensure that the value is protected when age_pages is reading it
        InterlockedExchange(&age_batch_sections, age_batch_size);
        SetEvent(age_needed);
    }
}

// Fault Handler
// Calling function needs to hold the pte_lock
BOOL
handle_soft_fault(PPTE pte, PULONG_PTR aligned_va, pfn_metadata ** official_meta, ULONG_PTR * official_pfn) {
    // Caller holds section lock
    PTE_SECTION *section = get_section(pte);

    // Get the pfn from the pte
    ULONG_PTR pfn = pte->transition.frame_number;

    // Use the pfn to access the pfn_metadata
    pfn_metadata *meta = find_pfn_from_frame_number(pfn);

    // Check if the page exists
    if (meta == NULL) {
        printf("SOFT FAULT: frame not found\n");
        DebugBreak();
        return FALSE;
    }

    // Get the page's own lock as page then list order
    EnterCriticalSection(&meta->lock);

    // Check the status of the PTE after releasing the lock as another thread could have altered its contents/status
    // Necessary because I set my access bit without a PTE section lock; therefore, I must check before I continue
    // If the PTE does not satisfy a soft fault, return to full_virtual memory to force another page fault and reroute
    // to correct function that can resolve the fault
    if (pte->transition.transition != 1 || pte->transition.frame_number != pfn) {
        LeaveCriticalSection (&meta->lock);
        return FALSE;
    }

    BOOL reclaim_disc_slot = FALSE;

    // If the disc is currently being written to disc, don't touch its list nodes but signal that the page soft faulted
    // Therefore, allowing the disc to release the slot
    if (meta->write_in_progress == 1) {
        // Can update the write_in_progress bit about the soft fault so the disc can later empty the stale slot
        meta->write_in_progress = 0;                                                                           
    }
    else if (meta->isOccupied == 3) {
        // Currently, on the standby list, remove it
        remove_entry_RW_list(&pfn_standby_list, meta);
        reclaim_disc_slot = TRUE;
    } else if (meta->isOccupied == 2) {
        // Check if its modified

        // On the modified list
        EnterCriticalSection(&pfn_modified_list.lock);
        ASSERT(meta->list.Flink != NULL);
        RemoveEntryList(&meta->list);
        // Clean flink blink data
        meta->list.Flink = NULL;
        meta->list.Blink = NULL;
        pfn_modified_list.size--;
        LeaveCriticalSection(&pfn_modified_list.lock);
    }

    activate_page(pte, meta, pfn);

    // Release the lock
    LeaveCriticalSection (&meta->lock);
    
    // Is it not already mapped; therefore, we only need to mark it as valid?
    if (MapUserPhysicalPages (aligned_va, 1, &pfn) == FALSE) {
        printf ("soft fault: full_virtual_memory_test : could not map VA %p to page %llX\n", aligned_va, pfn);
        DebugBreak();
        return FALSE;
    }

    // printf("[Thread %d] SOFT FAULT mapped VA %p to PFN %llx\n", my_thread_id, (void*)aligned_va, (unsigned long long)pfn);

    if (reclaim_disc_slot) {
        // Clean our disc metadata
        empty_disc_slot(meta->disc_index);
    }

    my_soft_faults++;

    // Using the addresses of the official metadata and pfn-update it's contents
    *official_meta = meta;
    *official_pfn = pfn;

    return TRUE;
}

// Calling function needs to hold the pte_lock
BOOL
handle_hard_fault(PPTE pte, PULONG_PTR aligned_va, pfn_metadata ** official_meta, ULONG_PTR * official_pfn) {
    // Caller holds section lock
    PTE_SECTION *section = get_section(pte);

    // This a zero PTE or a disc PTE
    // Find a free physical page in memory
    pfn_metadata * meta = get_free_page();

    // If we return NULL, we did not get a free page so try again
    while (meta == NULL) {
        // Release the pte section lock to allow the trim thread to work
        LeaveCriticalSection(&section->lock);
        InterlockedIncrement(&stall_count);

        // Wait for the trim to finish
        WaitForSingleObject(pages_available, INFINITE);

        EnterCriticalSection(&section->lock);

        // Check if another thread edited the pte to be valid or in transition. If so, punt
        if (pte->hardware.valid == 1 || pte->transition.transition == 1) {
            return FALSE;
        }

        meta = get_free_page();

    }

    // TODO: update my linear/random access to do some probability to go back to my recently viewed pages
    // TODO: based on its success, we will see how successful i am at trimming/ even aging for choosing the appropriate candidates
    // Get the frame number of the free page using pointer arithmetic
    ULONG_PTR pfn = find_frame_number_from_pfn((meta));

    // TODO: speculative reads. Idea that I am going linearly, to map several more pages then necessary
    // TODO: track my success on speculative reads which would then dictate further premptiveness extra mapping
    // Check if the thread's temp VA space is full. If so, unmap before getiing a free slot
    if (my_temp_va_count == TEMP_VA_SIZE) {
        // Get the starting va of the thread's temp va space
        PULONG_PTR temp_va_start = get_temp_va(0);
        if (MapUserPhysicalPages(temp_va_start, my_temp_va_count, NULL) == FALSE) {
            printf("Temp Va: batch unmap failed");
            DebugBreak();
        }
        my_temp_va_count = 0;
    }

    // Map the data on the page's data onto the temp page before freeing the page
    PULONG_PTR temp_va = get_temp_va(my_temp_va_count);

    // Map the frame to this temp va
    if (MapUserPhysicalPages(temp_va, 1, &pfn) == FALSE) {
        printf("hard fault: could not map scratch VA for page %llX\n", pfn);
        DebugBreak();
        return FALSE;
    }

    // If the pte was in disc state
    if (pte->disc.disc == 1) {
        // Give the pte is in disc state, it stores the disc index
        ULONG_PTR slot_on_disc = pte->disc.disc_index;

        // Check if slot_on_disc is an invalid disc address
        if (slot_on_disc >= disc_page_count) {
            printf("In handle hard fault: the slot_on_disc is invalid%llu\n", (unsigned long long)slot_on_disc);
            DebugBreak();
            return FALSE;
        }

        // Use memcpy to copy the data in the disc into the temp
        memcpy(temp_va, (char*)official_disc + slot_on_disc * PAGE_SIZE, PAGE_SIZE);
        // Mark the data in the disc slot to be invalid
        empty_disc_slot(slot_on_disc);
    } else {
        // Zeros the page if new
        memset(temp_va, 0, PAGE_SIZE);
    }

    // Update the thread's individual temp va counters and store the temp va for unmapping later
    my_temp_va_count++;

    activate_page(pte, meta, pfn);

    // Map the VA to the physical page
    if (MapUserPhysicalPages (aligned_va, 1, &pfn) == FALSE) {
        printf ("hard fault: full_virtual_memory_test : could not map VA %p to page %llX\n", aligned_va, pfn);
        DebugBreak();
        return FALSE;
    }

    // TODO: do disc bit maps for interlocked that can help get rid of the disk lock
    // TODO: look at noahs late stage wins
    // TODO: batch pull from standby to the free; therefore, use batch pull and repurpose thread
    // TODO: free list sections for each thread; therefore, use batch pull
    my_hard_faults++;

    *official_meta = meta;
    *official_pfn = pfn;

    return TRUE;
}

// BACCKGROUND THREADS
DWORD WINAPI
consumption_thread(LPVOID lpParam) {
    while (TRUE) {
        // Triggered by shut down or the routine time check
        DWORD r = WaitForSingleObject(shutdown_event, CONSUMPTION_TICK);

        // If shutdown, exit
        if (r == WAIT_OBJECT_0) {
            break;
        }

        consumption_rate();
    }
    return 0;
}


DWORD WINAPI
age_thread (LPVOID lpParam) {
    HANDLE wake[2] = { age_needed, shutdown_event };

    while (TRUE) {
        // Either triggered by the TIMEOUT or the age_needed event
        DWORD r = WaitForMultipleObjects(2, wake, FALSE, INFINITE);

        // Check if the trigger was a shutdown event
        if (r == WAIT_OBJECT_0 + 1) {
            break;
        }

        // Do an aging pass
        age_pages();
    }
    return 0;
}

// Trim thread triggered in two ways
// preemptive: TIMEOUT fires, checking if there is a need to trim before a page experiences no free pages
// emergency: preemptive trim failed, meaning a worker failed to find a free page
DWORD WINAPI
trim_thread (LPVOID lpParam) {
    HANDLE wake[2] = { trim_needed, shutdown_event };

    while (TRUE) {
        // Either triggered by the TIMEOUT or the trim_needed event
        DWORD r = WaitForMultipleObjects(2, wake, FALSE, INFINITE);

        // Check if the trigger was a shutdown event
        if (r == WAIT_OBJECT_0 + 1) {
            break;
        }

        InterlockedExchange(&trim_thread_active, 0);
        trim_pages();

        // Check if you need to wake the writer: if the batch size was met or if it is an emergency
        if (need_to_write() && (InterlockedCompareExchange(&write_thread_active, 1, 0) == 0)) {
            SetEvent(write_needed);
        }
    }

    return 0;
}

DWORD WINAPI
write_thread (LPVOID lpParam) {
    // Create an array of events that trigger the thread
    HANDLE events[2] = { write_needed, shutdown_event };

    while (TRUE) {
        // Triggered by either one
        DWORD result = WaitForMultipleObjects (2, events, FALSE, INFINITE);

        // If the event that triggered was shut_down event, exit.
        if (result == WAIT_OBJECT_0 + 1) {
            break;
        }

        InterlockedExchange(&write_thread_active, 0);

        // Double check if a write is actually warranted as states might have changed
        if (!need_to_write()) {
            if ((ULONG64)total_free_pages + pfn_standby_list.size > 0) {
                SetEvent(pages_available);
            }
            continue;
        }

        // Find how many avilable disc slots based on counter of filled pages
        // No need for lock as the worker threads that have access to these variables would only increase the amount
        // of empty slots; therefore, the number of available disc slots would increase which does not affect our code
        int available_disc_slots =  disc_page_count - filled_disc_slots;

        DEBUG_PRINT("[WRITE] modified=%llu avail_disc_slots=%d filled=%d/%llu\n",
        (unsigned long long)pfn_modified_list.size, available_disc_slots, filled_disc_slots, (unsigned long long)disc_page_count);

        // If there are no empty slots
        if (available_disc_slots <= 0) {
            // Disk is full so pages cannot go from modified to disk
            // printf("WRITE THREAD: Disk is full\n");

            // Check if there are free pages or standby list pages
            if (pfn_standby_list.size > 0 || (ULONG64)total_free_pages > 0) {
                // Allow repurpose and signal finished of trim and write thread
                SetEvent(pages_available);
                // Update the trim thread activity after pages are avaialble to prevent other thread's repeadtely calling
                // the trimmer thread unnecessarily
                InterlockedExchange(&trim_thread_active, 0);
            }

            continue;
        }

        // Mark pages to write to disk as write in progress and store into an array
        pfn_metadata * pages_to_write[WRITE_BATCH_SIZE];
        int num_pages_written = 0;

        EnterCriticalSection (&pfn_modified_list.lock);

        // Get the pointer to the head page of the modified list
        PLIST_ENTRY current = pfn_modified_list.entry.Flink;

        // Iterate through until we either do not have any pages in modifed or we reached batch size
        // Also, check if there is space in the disk
        while (num_pages_written < available_disc_slots &&
            num_pages_written < WRITE_BATCH_SIZE &&
            current != &pfn_modified_list.entry) {
            PLIST_ENTRY next = current->Flink;

            pfn_metadata * meta = (pfn_metadata *) current;

            // Try to get the lock
            if (TryEnterCriticalSection(&meta->lock) == FALSE) {
                current = next;
                continue;
            }

            // Make sure its still a modified page we hold
            if (meta->isOccupied != 2 || meta->write_in_progress != 0) {
                LeaveCriticalSection(&meta->lock);
                current = next;
                continue;
            }
            // Mark as writing to disk
            meta->write_in_progress = 1;

            // Move the page off the modified list so any other list cannot access it
            RemoveEntryList(&meta->list);
            // Clean flink blink data
            meta->list.Flink = NULL;
            meta->list.Blink = NULL;          
            pfn_modified_list.size--;
            meta->write_in_progress = 1;

            // Add to the array of pages to write to disk and increment counter
            pages_to_write[num_pages_written++] = meta;

            LeaveCriticalSection(&meta->lock);
            // Move onto the next one
            current = next;
        }

        LeaveCriticalSection (&pfn_modified_list.lock);

        ULONG_PTR disc_slots[WRITE_BATCH_SIZE];
        // Set every value in disk slot to -1 to ensure we can precisely identify a failed map later on
        for (int i = 0; i < WRITE_BATCH_SIZE; i++) {
            disc_slots[i] = (ULONG_PTR) -1;
        }

        // Update the write thread active status
        write_to_disk(num_pages_written, pages_to_write, disc_slots);
        DIAG_INC(write_call_count);
        DIAG_ADD64(write_total_pages, num_pages_written);


        // Determine the pfn_state of each page written to disk based on the write_in_progress bit
        // Specifically watching out for a soft fault that happened during the write to disk
        for (int i = 0; i < num_pages_written; i++) {
            // Iterate through each entry
            pfn_metadata * meta = (pfn_metadata *) pages_to_write[i];
            ULONG_PTR slot = disc_slots[i];

            // If the map failed or the disk is full, there is nothing to free or write to disk for
            if (slot == -1) {
                meta->write_in_progress = 0;
                continue;
            }

            EnterCriticalSection(&meta->lock);

            // Check if the write in progress is invalid, meaning there was a soft fault mid-write
            if (meta->write_in_progress == 0) {
                LeaveCriticalSection(&meta->lock);
                // Disc slot no longer needed and stale
                empty_disc_slot(slot);
                continue;
            }
            // Or the page was successfully written in disk and now trasitioning to disk state
            // Write data to disk
            meta->disc_index = slot;

            meta->write_in_progress = 0;
            // In disk state
            meta->isOccupied = 3;

            // Move the page into the disk linked list and update counter
            insert_tail_RW_list (&pfn_standby_list, meta);
            LeaveCriticalSection(&meta->lock);
        }

        // Tell the get_free_page function that there are now standby lists ready if needed
        SetEvent(pages_available);
    }
    return 0;
}


// WINDOWS
BOOL
// Grants a Windows security privilege that permits calling certain methods (i.e. AllocateUserPhysicalPages)
// Without AWE API won't run as its not granted by default
// Return boolean (true, success)
GetPrivilege  (
    VOID
    )
{
    struct {
        DWORD Count;
        LUID_AND_ATTRIBUTES Privilege [1];
    } Info;

    //
    // This is Windows-specific code to acquire a privilege.
    // Understanding each line of it is not so important for
    // our efforts.
    //

    HANDLE hProcess;
    HANDLE Token;
    BOOL Result;

    //
    // Open the token.
    //

    hProcess = GetCurrentProcess ();

    Result = OpenProcessToken (hProcess,
                               TOKEN_ADJUST_PRIVILEGES,
                               &Token);

    if (Result == FALSE) {
        printf ("Cannot open process token.\n");
        return FALSE;
    }

    //
    // Enable the privilege.
    //

    Info.Count = 1;
    Info.Privilege[0].Attributes = SE_PRIVILEGE_ENABLED;

    //
    // Get the LUID.
    //

    Result = LookupPrivilegeValue (NULL,
                                   SE_LOCK_MEMORY_NAME,
                                   &(Info.Privilege[0].Luid));

    if (Result == FALSE) {
        printf ("Cannot get privilege\n");
        return FALSE;
    }

    //
    // Adjust the privilege.
    //

    Result = AdjustTokenPrivileges (Token,
                                    FALSE,
                                    (PTOKEN_PRIVILEGES) &Info,
                                    0,
                                    NULL,
                                    NULL);

    //
    // Check the result.
    //

    if (Result == FALSE) {
        printf ("Cannot adjust token privileges %u\n", GetLastError ());
        return FALSE;
    }

    if (GetLastError () != ERROR_SUCCESS) {
        printf ("Cannot enable the SE_LOCK_MEMORY_NAME privilege - check local policy\n");
        return FALSE;
    }

    CloseHandle (Token);

    // Close
    return TRUE;
}

#if SUPPORT_MULTIPLE_VA_TO_SAME_PAGE

// A object that acts as a container for physical pages. Allows the same physical page to be simutaneously mapped
// At multiple virtual addresses. Only compiled when support_multiple_VA_TO_SAME_PAGE is 1
HANDLE
CreateSharedMemorySection (
    VOID
    )
{
    HANDLE section;
    MEM_EXTENDED_PARAMETER parameter = { 0 };

    //
    // Create an AWE section.  Later we deposit pages into it and/or
    // return them.
    //

    parameter.Type = MemSectionExtendedParameterUserPhysicalFlags;
    parameter.ULong = 0;

    section = CreateFileMapping2 (INVALID_HANDLE_VALUE,
                                  NULL,
                                  SECTION_MAP_READ | SECTION_MAP_WRITE,
                                  PAGE_READWRITE,
                                  SEC_RESERVE, // Reserve address space but don't commit physical address yet
                                  0,
                                  NULL,
                                  &parameter,
                                  1);

    return section;
}

#endif

ULONG_PTR
trim_a_section(PTE_SECTION *section, int section_batch_size, int include_access_bit, int wait_for_lock) {
    // Check if no need to trim any more pages
    if (section_batch_size <= 0) {
        return 0;
    }

    if (wait_for_lock) {
        // Wait for the section lock
        EnterCriticalSection(&section->lock);
    } else {
        // Failed to accquire section lock, move onto next section
        if (TryEnterCriticalSection(&section->lock) == FALSE) {
            return 0;
        }
    }

    // Initialize array to keep track of pages to trim
    pfn_metadata * trimmed_pages[TRIM_BATCH_SIZE];
    PVOID trimmed_pages_vas[TRIM_BATCH_SIZE];
    // Keep track of how many pages trimmed in this section
    int sec_pages_trimmed = 0;

    for (int age = NUM_AGES - 1; age >= 0 && sec_pages_trimmed < section_batch_size; age--) {
        // Go through each list from the end to skipp any pages with a valid access bit
        // Therefore, the recently accessed pages will remain on the list until the ager moves it accorindlgy
        PLIST_ENTRY current = section->age_lists[age].Flink;
        // Refresh the value of the head
        PLIST_ENTRY head = &section->age_lists[age];
        // TODO: wth is hapepnign with my ageListCounts
        // Continue while there are pages on the age list or while number of pages trimmed is less than batch size
        while (current != head && sec_pages_trimmed < section_batch_size) {
            // Grab the next pointer
            PLIST_ENTRY next = current->Flink;

            // Get the pfn metadata of the current page we might trim to get PTE
            pfn_metadata * trim_page = (pfn_metadata *) current;

            // Check that the entry we got is a valid pfn, and is not stale
            if (trim_page < pfn_table || trim_page > &pfn_table[max_frame_number]) {
                DebugBreak();
                break;
            }
            PPTE pte = trim_page->pte;

            // FInd the state of the PTE
            PTE old;
            old.entire = pte->entire;

            // If its valid and access bit is 1, skip it and leave it linked; therefore, advance to the next page
            // If its pass 2, we need pages; therefore, we will include hot pages into the triming candidates
            if (!include_access_bit && old.hardware.valid == 1 && old.hardware.access == 1) {
                // Move onto the next page (the current page's FLINK) as we are going from back to forward
                current = next;
                continue;
            }

            // Modify the PTE from valid to transition
            // Build the transition state PTE
            PTE new;
            new.entire = 0;
            new.transition.valid = 0;
            new.transition.transition = 1;
            new.transition.frame_number = find_frame_number_from_pfn(trim_page);

            // Only update it if its state reamins valid with access bit invalid to ensure a worker thread didn't change it
            ULONG_PTR actual_value = (ULONG_PTR) InterlockedCompareExchange64(
                (volatile LONG64 *) &pte->entire,
                (LONG64) new.entire, (LONG64) old.entire);

            // A worker thread changed its state meaning no longer viable to trim
            if (actual_value != old.entire) {
                // If it failed, the worker thread likely touched it mid trim so just move on
                current = next;
                continue;
            }

            // TODO: based on how many we trimmed in our big loop, that means we failed; then try again without the access bit restriction
            // uses a local to figure out ANY _PAGES

            // Sucessfuly mapped the page to transition state
            // Remove from its age list

            RemoveEntryList(&trim_page->list);
            // Clean the flink blink data
            trim_page->list.Flink = NULL;
            trim_page->list.Blink = NULL;
            section->age_counts[age]--;

            trimmed_pages_vas[sec_pages_trimmed] = get_va_from_pte(pte);
            trimmed_pages[sec_pages_trimmed] = trim_page;

            sec_pages_trimmed++;

            // Move onto next page
            current = next;
        }
    }

    if (sec_pages_trimmed > 0) {
        // While holding the section's lock, unmap the batch before another thread accesses it
        if (MapUserPhysicalPagesScatter(trimmed_pages_vas, sec_pages_trimmed, NULL) == FALSE) {
            printf("TRIM_PAGES: scatter unmapped failed\n");
        }

        // Update the trimmed_pages to the modified status
        EnterCriticalSection(&pfn_modified_list.lock);
        for (int i = 0; i < sec_pages_trimmed; i++) {
            pfn_metadata * meta = trimmed_pages[i];

            // Get the page lock
            if (TryEnterCriticalSection(&meta->lock) == FALSE) {
                continue;
            }

            PPTE pte = meta->pte;
            ULONG_PTR pfn = find_frame_number_from_pfn(meta);

            // Check if soft fault reactivated it
            if (pte->transition.transition != 1 || pte->transition.frame_number != pfn) {
                LeaveCriticalSection(&meta->lock);
                continue;
            }

            // Double check the status in fear of a soft fault
            if (meta->isOccupied != 1) {
                LeaveCriticalSection(&meta->lock);
                continue;
            }

            meta->isOccupied = 2;
            // Add onto modified list
            InsertTailList(&pfn_modified_list.entry, &meta->list);
            pfn_modified_list.size++;
            LeaveCriticalSection(&meta->lock);
        }
        LeaveCriticalSection(&pfn_modified_list.lock);
    }

    LeaveCriticalSection(&section->lock);
    return sec_pages_trimmed;
}
// Create a event
VOID
trim_pages() {
    // Query Performance Frequency: tells how many ticks per second the performance counter useees
    // Query PerformanceCounter: gives the slapsed ticks
    DIAG_DECL(LARGE_INTEGER t0; LARGE_INTEGER t1;)
    DIAG_QPC(&t0);

    // Read and clear the emergency flag. Use it for stats and trimming batch_size
    int was_emergency = (int) InterlockedExchange(&trim_is_emergency, 0);
    if (was_emergency) {
        InterlockedIncrement(&emergency_trim_count);
    } else {
        InterlockedIncrement(&preemptive_trim_count);
    }

    ULONG_PTR batch_size = compute_trim_size(was_emergency);

    // Prevent redundancy
    if (batch_size <= 0) {
        return;
    }

    int total_pages_trimmed = 0;

    // Pass 1: optimized to skip any pages with an access bit or any section that it can't get it's section lock on the first try
    // Essentially skip busy PTE sections and hot pages
    for (int i = 0; i < NUM_PTE_SECTIONS && total_pages_trimmed < batch_size; i++) {
        int section_batch_size = batch_size - total_pages_trimmed;
        total_pages_trimmed += trim_a_section(&pte_sections[i], section_batch_size, FALSE, FALSE);
    }

    // Pass 2: if Pass 1 failed to trim enough. wait for every section lock and ignore the access bit
    if (total_pages_trimmed < batch_size) {
        for (int i = 0; i < NUM_PTE_SECTIONS && total_pages_trimmed < batch_size; i++) {
            int section_batch_size = batch_size - total_pages_trimmed;
            total_pages_trimmed += trim_a_section(&pte_sections[i], section_batch_size, TRUE, TRUE);
        }
    }
    DIAG_QPC(&t1);
    DIAG_ADD64(trim_total_qpc, t1.QuadPart - t0.QuadPart);
    DIAG_INC(trim_call_count);
    DIAG_ADD64(trim_total_pages, total_pages_trimmed);

    if (was_emergency) {
        InterlockedAdd64(&emergency_pages_trimmed, total_pages_trimmed);
    } else {
        InterlockedAdd64(&preemptive_pages_trimmed, total_pages_trimmed);
    }
}

//
BOOL
initialize_system() {
    printf("Setting up system \n");

    // Return value from AllocateUserPhysicalPages
    BOOL allocated;
    // Return value from GetPrivilege()
    BOOL privilege;
    BOOL obtained_pages;
    // Can be a shared memory section handle (multi-VA) or the process handle (single-VA)
    HANDLE physical_page_handle;
    ULONG_PTR virtual_address_size;

    initialize_list_head(&pfn_modified_list);
    initialize_RW_list(&pfn_standby_list);
    for (int i = 0; i < NUM_THREADS; i++) {
        initialize_list_head(&free_lists[i]);
    }

    // Initialize the multithreading locks
    InitializeCriticalSectionAndSpinCount(&disc_lock, 0x00FFFFFF);

    // MULTITHREADING TESTER
    // manual-reset=NULL, starts unsignaled=FALSE
    shutdown_event = CreateEvent(NULL, TRUE, FALSE, NULL);
    if (shutdown_event == NULL) {
        printf("CreateEvent failed: %u\n", GetLastError());
        return FALSE;
    }

    //
    // Allocate the physical pages that we will be managing.
    //
    // First acquire privilege to do this since physical page control
    // is typically something the operating system reserves the sole
    // right to do.
    //

    privilege = GetPrivilege();

    if (privilege == FALSE) {
        printf ("full_virtual_memory_test : could not get privilege\n");
        return FALSE;
    }

#if SUPPORT_MULTIPLE_VA_TO_SAME_PAGE

    // Multiple VA
    physical_page_handle = CreateSharedMemorySection ();

    if (physical_page_handle == NULL) {
        printf ("CreateFileMapping2 failed, error %#x\n", GetLastError());
        return FALSE;
    }

#else
    // Single VA
    physical_page_handle = GetCurrentProcess ();

#endif

    // Create an array big enough to hold one ULONG_PTR per physical page, it will store a PFN
    physical_page_numbers = malloc (physical_page_count * sizeof (ULONG_PTR));

    if (physical_page_numbers == NULL) {
        printf ("full_virtual_memory_test : could not allocate array to hold physical page numbers\n");
        return FALSE;
    }

    // Ask OS to allocate physical pages and share the PFN
    // &: gives you the address of any function
    // pointer: stores location
    // * of pointer: stores value
    allocated = AllocateUserPhysicalPages (physical_page_handle,
                                           &physical_page_count, // input: how many you want
                                           // output: how many you actually got as RAM limited
                                           physical_page_numbers); // output: array of PFNs

    if (allocated == FALSE) {
        printf ("full_virtual_memory_test : could not allocate physical pages\n");
        return FALSE;
    }

    if (physical_page_count != NUMBER_OF_PHYSICAL_PAGES) {

        printf ("full_virtual_memory_test : allocated only %llu pages out of %llu pages requested\n",
                (unsigned long long)physical_page_count,
                (unsigned long long)NUMBER_OF_PHYSICAL_PAGES);

        if (physical_page_count == 0) {
            printf ("Recieved no pages\n");
            return FALSE;
        }
    }

    // Initializing the pfn_metadata and adding it to the free_list
    setup_pfn_table(physical_page_numbers, physical_page_count);

    //
    // Reserve a user address space region using the Windows kernel
    // AWE (address windowing extensions) APIs.
    //
    // This will let us connect physical pages of our choosing to
    // any given virtual address within our allocated region.
    //
    // We deliberately make this much larger than physical memory
    // to illustrate how we can manage the illusion.
    //

    // Allocate memory for our disk
    official_disc = create_page_file(&disc_page_count);

    // Calculate the size of our virtual memory space based on the physical pages (memory + disk) available
    virtual_address_size = (physical_page_count + disc_page_count - 1) * PAGE_SIZE;

    // Round down to a PAGE_SIZE boundary.
    virtual_address_size &= ~(PAGE_SIZE - 1);
    virtual_address_size_in_unsigned_chunks = virtual_address_size / sizeof (ULONG_PTR);

// Multiple VA
#if SUPPORT_MULTIPLE_VA_TO_SAME_PAGE

    MEM_EXTENDED_PARAMETER parameter = { 0 };

    //
    // Allocate a MEM_PHYSICAL region that is "connected" to the AWE section
    // created above.
    //

    parameter.Type = MemExtendedParameterUserPhysicalHandle;
    parameter.Handle = physical_page_handle;

    start = VirtualAlloc2 (NULL,
                       NULL,
                       virtual_address_size,
                       MEM_RESERVE | MEM_PHYSICAL, // MEM_PHYSICAL: reserve VA space that can be explicitly mapped
                       // to physical pages using MapUserPhysicalPages
                       PAGE_READWRITE,
                       &parameter,
                       1);

    MEM_EXTENDED_PARAMETER sys_param = { 0 };
    sys_param.Type = MemExtendedParameterUserPhysicalHandle;
    sys_param.Handle = physical_page_handle;

    system_va_start = VirtualAlloc2(
        NULL, NULL,
        WRITE_BATCH_SIZE  * PAGE_SIZE,  // One slot per possible PFN
        MEM_RESERVE | MEM_PHYSICAL,
        PAGE_READWRITE,
        &sys_param, 1);

    if (system_va_start == NULL) {
        printf("Could not allocate system VA\n");
        return FALSE;
    }

    MEM_EXTENDED_PARAMETER fault_param = { 0 };
    fault_param.Type = MemExtendedParameterUserPhysicalHandle;
    fault_param.Handle = physical_page_handle;

    // Allocate enough memory such that each hard fault thread has one page to work with
    fault_va_start = VirtualAlloc2(
        NULL, NULL,
        NUM_THREADS * PAGE_SIZE * TEMP_VA_SIZE,
        MEM_RESERVE | MEM_PHYSICAL,
        PAGE_READWRITE,
        &fault_param, 1);

    if (fault_va_start == NULL) {
        printf("Could not allocate fault scratch VA\n");
        return FALSE;
    }
#else

    start = VirtualAlloc (NULL,
                      virtual_address_size,
                      MEM_RESERVE | MEM_PHYSICAL,
                      PAGE_READWRITE);

#endif

    if (start == NULL) {

        printf ("full_virtual_memory_test : could not reserve memory %x\n",
                GetLastError());

        return FALSE;
    }

    //
    // Now perform random accesses.
    //

    // Initialize and reserve the space for the page table
    page_table = zero_malloc((virtual_address_size / PAGE_SIZE) * sizeof(PTE));

    ULONG_PTR num_ptes = virtual_address_size / PAGE_SIZE;
    ptes_per_section = (num_ptes + NUM_PTE_SECTIONS - 1) / NUM_PTE_SECTIONS;

    // If integer division results in 0, only enough PTE's for one section
    if (ptes_per_section == 0) {
        ptes_per_section = 1;
    }

    pte_sections = malloc(NUM_PTE_SECTIONS * sizeof(PTE_SECTION));

    // Check if we properly reserved memory
    if (pte_sections == NULL) {
        printf("in initialize_system, we failed to allocate PTE sections");
        return FALSE;
        DebugBreak();
    }

    // Iterate through each pte_section and initialize its respective lock
    for (int i = 0; i < NUM_PTE_SECTIONS; i++) {
        InitializeCriticalSectionAndSpinCount(&pte_sections[i].lock, 0x00FFFFFF);

        // Iterate through each pte_section's age lists to initilize list_heads and counters
        for (int j = 0; j < NUM_AGES; j++) {
            InitializeListHead(&pte_sections[i].age_lists[j]);
            pte_sections[i].age_counts[j] = 0;
        }
    }
    return TRUE;
}


VOID
check_accuracy() {
    printf("Checking the accuracy of stamped data\n");
    int num_corrupt_page = 0;
    int bad_valid = 0, bad_trans = 0, bad_disc = 0;

    for (ULONG_PTR i = 0; i < virtual_address_size_in_unsigned_chunks; i += CHUNKS_PER_PAGE) {
        // Get my VA
        PULONG_PTR va = start + i;
        // Get PTE
        PPTE pte = get_pte_from_va(va);

        // If valid PTE, then the data inside the page should equal VA
        if (pte->hardware.valid == 1) {
            if ((ULONG_PTR) va != *va) {
                num_corrupt_page++;
                bad_valid++;
            }
        }
        // If transition PTE, then re-map and access data
        else if (pte->transition.transition == 1) {
            ULONG_PTR pfn = pte->transition.frame_number;

            // Map the pages to soft fault
            if (MapUserPhysicalPages(va, 1, &pfn) == FALSE) {
                printf("Check accuracy: wanted to remap the pages but failed");
                return;
            }

            if ((ULONG_PTR) va != *va) {
                num_corrupt_page++;
                bad_trans++;
            }
        }
        // DISK -> using the disk index function, access data in the disk and compare it to the stored VA
        else if (pte->disc.disc == 1) {
            ULONG_PTR slot = pte->disc.disc_index;

            if (slot >= disc_page_count) {
                printf("Check accuracy: invalid disc slot");
                continue;
            }

            // Point at the first ULONG_PTR of that disk page
            PULONG_PTR disc_data = (PULONG_PTR)((char*)official_disc + slot * PAGE_SIZE);

            if (*disc_data != (ULONG_PTR) va) {
                num_corrupt_page++;
                bad_disc++;
            }
        }
        // if 0, just move on
        else {
            continue;
        }
    }

    // Print final stats
    if (num_corrupt_page == 0) {
        printf("Check accuracy is all good.\n");
    } else {
        printf("Check accuracy failed %d corrput\n", num_corrupt_page);
        printf("corrupt: valid=%d trans=%d disc=%d\n", bad_valid, bad_trans, bad_disc);
        DebugBreak();
    }
}

VOID
cleanup() {
    // Delete the multithreading locks
    DeleteCriticalSection(&disc_lock);

    if (pte_sections != NULL) {
        // Iterate through the PTE sections to delete its own respective locks
        for (int i = 0; i < NUM_PTE_SECTIONS; i++) {
            DeleteCriticalSection(&pte_sections[i].lock);
        }
        pte_sections = NULL;
    }

    //
    // Now that we're done with our memory we can be a good
    // citizen and free it.
    //
    VirtualFree(start, 0, MEM_RELEASE);
    VirtualFree(system_va_start, 0, MEM_RELEASE);
    VirtualFree(fault_va_start, 0, MEM_RELEASE);
}

// Return type: DWORD (unsigned int (32 bit)) WINAPI (set of rules that tells compiler how to push variables into stack)
DWORD WINAPI
// Program manually controls physical pages. Manage a group of physical pages, connect/disconnect to VA
full_virtual_memory_test (
    LPVOID thread_id // Pass a blank pointer
    )
{
    unsigned i;
    PULONG_PTR arbitrary_va;
    unsigned random_number;
    BOOL page_faulted;
    BOOL fault_resolution = TRUE;
    int successful_accesses = 0;

    // Save the thread_id
    int curr_thread = (int)(intptr_t)thread_id;
    // Store it into the global variable
    my_thread_id = curr_thread;

    printf("Thread %d entered full virtual memory_test \n", curr_thread);

    // Initialize a local thread for random function
    THREAD_RNG_STATE my_rng;
    InitializeThreadRNG(&my_rng);

    LOCALITY_STATE loc;
    ULONG_PTR total_pages = virtual_address_size_in_unsigned_chunks / CHUNKS_PER_PAGE;
    init_locality_state(&loc, &my_rng, total_pages);

    // Start the individual threads timer
    LARGE_INTEGER frequency, start, end;
    QueryPerformanceFrequency(&frequency);
    QueryPerformanceCounter(&start);

    for (i = 0; i < MB(1) ; i += 1) {
        //
        // Randomly access different portions of the virtual address
        // space we obtained above.
        //
        // If we have never accessed the surrounding page size (4K)
        // portion, the operating system will receive a page fault
        // from the CPU and proceed to obtain a physical page and
        // install a PTE to map it - thus connecting the end-to-end
        // virtual address translation.  Then the operating system
        // will tell the CPU to repeat the instruction that accessed
        // the virtual address and this time, the CPU will see the
        // valid PTE and proceed to obtain the physical contents
        // (without faulting to the operating system again).
        //

        // If the arbitrary VA is empty or successfully stamped, generate the next arbitrary VA
        if (fault_resolution == TRUE) {
            arbitrary_va = locality_next_va(&loc, &my_rng, total_pages);
            fault_resolution = FALSE;
        }

        page_faulted = FALSE;

        // If v=0, will lead to the except, leading to the mapping or if not using the same page that was already mapped
        __try {
            // Stamping the page which is useful for the check
            *arbitrary_va = (ULONG_PTR) arbitrary_va;

            successful_accesses++;
            // Mark the arbitrary VA as successfully mapped
            fault_resolution = TRUE;
        } __except (EXCEPTION_EXECUTE_HANDLER) {
            // Flag page fault
            page_faulted = TRUE;
        }

        if (page_faulted) {
            // If page faulted, we want to redo this iteration to confirm succesfully mapping
            i--;

            // Get the PTE
            PPTE pte = get_pte_from_va(arbitrary_va);
            // Find the PTE section lock
            PTE_SECTION * section = get_section(pte);

            EnterCriticalSection(&section->lock);
            // During multithreading, this check prevents a thread from repeating the mapping process done by another thread
            if (pte->hardware.valid == 1) {
                LeaveCriticalSection(&section->lock);
                continue;
            }

            pfn_metadata * meta;
            ULONG_PTR pfn;
            PULONG_PTR aligned_va = get_va_from_pte(pte);

            // Check if in transition state (pfn is in modified or standby)
            if (pte->transition.transition == 1) {
                // Hold the pte section lock when calling fault_resolution
                fault_resolution = handle_soft_fault(pte, aligned_va, &meta, &pfn);
            }
            // DISK FAULT or ZERO FAULT
            // Check if invalid, not transition state, and in disk
            else {
                fault_resolution = handle_hard_fault(pte, aligned_va, &meta, &pfn);
            }

            if (fault_resolution == FALSE) {
                //printf("The page fault resolution failed. Try again.\n");
                // If the page did not get resolved, we will page fault again to try to resolve
                LeaveCriticalSection(&section->lock);
                // Try again, meaning i never increments
                i--;
                continue;
            }

            // Update pte and pte section metadata
            set_pte_valid(section, meta, pte, pfn);
            LeaveCriticalSection(&section->lock);

            // Don't advance to the next VA, retry to make sure successful stamping of data
            fault_resolution = FALSE;
            if (i % 100000 == 0) printf(".");
        } else {
            locality_advance(&loc);

            // Reset pte age bit an
            PPTE pte = get_pte_from_va(arbitrary_va);

            // Successfuly accessed an already mapped page; update the access bit
            // No need to move it into a different age bucket during the worker thread as it is costly
            // Instead, during an aging thread's run, it will use the access to change to list 0.
            set_access_bit(pte);

            // TODO: check if the age is not 0 before acquring the lock
            // TODO: update the pte and not the buckets, and update buckets in the ager
            // TODO: see how to do interlocked for pte
            // TODO: add a access bit in hardware, to help me get rid of the age bits in the pte
            // TODO: can reset in the really hot function really fast, and only update in the ager.
            // TODO: cfor the trimmer, just check if its acceess bit is 0 when trimming
        }
    }
    // TODO: fix the linear function such that it goes back to old linear stuff
    // TODO: consumption rate for aging, don't really age if

    // Stop the individual thread timer
    QueryPerformanceCounter(&end);

    // Save the thread's totals into global array
    final_results[curr_thread].thread_id = curr_thread;
    final_results[curr_thread].hard_faults = my_hard_faults;
    final_results[curr_thread].soft_faults = my_soft_faults;
    final_results[curr_thread].repurpose_faults = my_repurpose_faults;
    final_results[curr_thread].total_accesses = successful_accesses;
    final_results[curr_thread].elapsed_ms =  (double)(end.QuadPart - start.QuadPart) * 1000.0 / frequency.QuadPart;

    return 0;
}

int
main (
    int argc,
    char** argv
    )
{

    //
    // Test our very complicated usermode virtual implementation.
    //
    // We will control the virtual and physical address space management
    // ourselves with the only two exceptions being that we will :
    //
    // 1. Ask the operating system for the physical pages we'll use to
    //    form our pool.
    //
    // 2. Ask the operating system to connect one of our virtual addresses
    //    to one of our physical pages (from our pool).
    //
    // We would do both of those operations ourselves but the operating
    // system (for security reasons) does not allow us to.
    //
    // But we will do all the heavy lifting of maintaining translation
    // tables, PFN data structures, management of physical pages,
    // virtual memory operations like handling page faults, materializing
    // mappings, freeing them, trimming them, writing them out to backing
    // store, bringing them back from backing store, protecting them, etc.
    //
    // This is where we can be as creative as we like, the sky's the limit !
    //

    if (initialize_system() == FALSE) {
        printf("Initialization failed\n");
        DebugBreak();
        return 0;
    }

    // Start timer for the total run time
    LARGE_INTEGER total_frequency, total_start, total_end;
    QueryPerformanceFrequency(&total_frequency);
    QueryPerformanceCounter(&total_start);

    // Set up the events
    trim_needed = CreateEvent(NULL, FALSE, FALSE, NULL);
    write_needed = CreateEvent(NULL, FALSE, FALSE, NULL);
    age_needed = CreateEvent(NULL, FALSE, FALSE, NULL);
    pages_available = CreateEvent(NULL, TRUE, FALSE, NULL);

    // Trim Thread
    HANDLE trim_thread_handle  = CreateThread(NULL, 0, trim_thread, NULL, 0, NULL);

    HANDLE write_thread_handle = CreateThread(NULL, 0, write_thread, NULL, 0, NULL);

    HANDLE age_thread_handle = CreateThread(NULL, 0, age_thread, NULL, 0, NULL);

    HANDLE consumption_thread_handle = CreateThread(NULL, 0, consumption_thread, NULL, 0, NULL);
    HANDLE thread_handles[NUM_THREADS];
    DWORD thread_ids[NUM_THREADS];

    // Iterate through each thread to call createThread and store in array
    for (int i = 0; i < NUM_THREADS; i++) {
        thread_ids[i] = i;
        thread_handles[i] = CreateThread (NULL, 0, full_virtual_memory_test,
            // Pass the loop variable i which is the thread's ID. i is 32 bit but return type is 64;
            // therefore, the intptr_t stretches i into a 64-bit int. LPVOID labels the value as a memory address
            (LPVOID)(intptr_t) thread_ids[i],
            0, &thread_ids[i]);

        if (thread_handles[i] == NULL) {
            printf("Error creating thread %d\n", i);
        }
    }

    // Wait indefinetly for all worker threads to complete
    DWORD wait_result = WaitForMultipleObjects (NUM_THREADS, thread_handles, TRUE, INFINITE);

    if (wait_result == WAIT_FAILED) {
        printf("WaitForMultipleObjects failed\n");
    } else {
        printf("WaitForMultipleObjects succeeded\n");
    }

    // Stop the timer for the total workload
    QueryPerformanceCounter(&total_end);
    double total_ms = (double)(total_end.QuadPart - total_start.QuadPart) * 1000.0 / total_frequency.QuadPart;

    // Tell all background threads to stop
    SetEvent(shutdown_event);

    // Wait for the background threads to exit
    WaitForSingleObject(write_thread_handle, INFINITE);
    WaitForSingleObject(trim_thread_handle, INFINITE);
    WaitForSingleObject(age_thread_handle, INFINITE);
    WaitForSingleObject(consumption_thread_handle, INFINITE);

    check_accuracy();

    // Close all thread handles and print out their stats
    for (int i = 0; i < NUM_THREADS; i++) {
        CloseHandle(thread_handles[i]);
    }

    // Close background thread handles
    CloseHandle(trim_thread_handle);
    CloseHandle(write_thread_handle);
    CloseHandle(age_thread_handle);

    CloseHandle(consumption_thread_handle);

    // Close events
    CloseHandle(trim_needed);
    CloseHandle(write_needed);
    CloseHandle(age_needed);
    CloseHandle(pages_available);
    CloseHandle(shutdown_event);

    // Print Stats
    printf("\nRESULTS\n");
    long long all_accesses = 0;

    for (int i = 0; i < NUM_THREADS; i++) {
        printf("Thread %d:\n", i);
        printf ("full_virtual_memory_test : finished accessing %u random virtual addresses\n", final_results[i].total_accesses);
        printf("Hard Faults: %d\n", final_results[i].hard_faults);
        printf("Soft-Faults: %d\n", final_results[i].soft_faults);
        printf("Repurpose Faults: %d\n", final_results[i].repurpose_faults);
        printf("Time: %.2f ms\n", final_results[i].elapsed_ms);
        all_accesses += final_results[i].total_accesses;
        printf("\n");
    }

    printf("\nTRIM BEHAVIOR\n");
    printf("Preemptive trims: %ld (%lld pages)\n",
        preemptive_trim_count, (long long)preemptive_pages_trimmed);
    printf("Emergency trims:  %ld (%lld pages)\n",
        emergency_trim_count, (long long)emergency_pages_trimmed);
    printf("Worker stalls (blocked on pages_available): %ld\n", stall_count);
    long total_trims = preemptive_trim_count + emergency_trim_count;
    if (total_trims > 0) {
        printf("Emergency ratio: %.1f%%\n",
            100.0 * emergency_trim_count / total_trims);
    }
    #if DIAGNOSTICS
        LARGE_INTEGER freq; QueryPerformanceFrequency(&freq);
        double trim_ms = 1000.0 * trim_total_qpc / freq.QuadPart;
        double wmap_ms = 1000.0 * write_map_qpc  / freq.QuadPart;
        double wcpy_ms = 1000.0 * write_memcpy_qpc / freq.QuadPart;
        printf("new sttats to determine if im failing on the algorithms or the numbers or both\n");
        printf("TRIM:  %d calls, %.1f ms total, %.3f ms/call, %.1f pages/call\n",
            trim_call_count, trim_ms, trim_call_count ? trim_ms / trim_call_count : 0.0,
            trim_call_count ? (double)trim_total_pages / trim_call_count : 0.0);
        printf("WRITE: %d calls, map=%.1fms memcpy=%.1fms, %.1f pages/call\n",
            write_call_count, wmap_ms, wcpy_ms,
            write_call_count ? (double)write_total_pages / write_call_count : 0.0);
#endif

    printf("Total time for all workload: %.2f ms\n", total_ms);
    printf("Total accesses across threads: %lld\n", all_accesses);

    cleanup();
    return 0;
}

// Create a free list of empty disc slot
    // a) create a disc struct and linked list
    // b) create a stack struct and use a stack that tracks the indicies of empty disc array slots
// Create target functions
    // create target functions
    // faulting and the arbitrary function
    // aging
    // trimming
    // modified page writing

// 1. Update the PTE from valid to transition (Soft Fault: transition bit in PTE)
// 2. Put the PFN onto an array to write to disk (essentially, batch a bunch to go to disk)
// 2. Finding the page data that we want to bring to disk, looking at PFN metadata
// *Flink blink: chain through the PFN in any order
// Linear walk: For loop that iterates through looking at the bit
// Array to keep track of
// 3. write to disk: memCopy. In PFN metadata, store disk address.
// 4. move PFN from modified to standby list: inc/dec counter
// 5a. If soft fault (same VA/frame number): move from modified/standby list and valid bit in PTE
// and transition bit is 0. Map.
// 5b. repurposed PFN: modify old PTE from transition bit to 0 and disk bit to 1 and modify old PTE
// PFN to a disk address. And Map new stuff.

// TODO: add a zero list
// TODO: do a temporary trim and write, and then after regardless, do a fat batch
// TODO: add counter for how many emergency v. preemptive thread
// TODO: QUESTION: is this truly necessary but modified list can chagne any time
// TODO: optimize the aging locks to interlocked later
// TODO: speculative faulting, doing more than needed
// TODO: assign priority to threads


// TODO: break into files
// TODO: do we even need the 1 bit for 0 and active in pfn
// TODO: if we are bound to take care of a standby list, we can just do extra
// TODO: wtf do i never add stuff on the free_list
// TODO: more free list?
// TODO: add a thread to do memset 0 in the background


// TODO: zero thre_thread that does memset for zero and standby
// TODO: if we can repurpose something, we can repurpose a few more
// TODO: multiple free list, one standby list
// TODO: if im going to get a free list, the thread might as well grab a few more and put into the cache
// TODO: instead of free list for each thread, just do tryEnter and do communal bunches