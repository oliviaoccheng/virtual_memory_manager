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

// TODO: multithreading: all window api
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

// MULTITHREADING HELPER FUNCTIONS
// This is the function your threads will execute
HANDLE shutdown_event;
#define NUMBER_OF_PHYSICAL_PAGES (MB(64) / PAGE_SIZE) // ((VIRTUAL_ADDRESS_SIZE / PAGE_SIZE) / 64)
#define NUM_DISC_PAGES (NUMBER_OF_PHYSICAL_PAGES * 4) // (MB(2) / PAGE_SIZE)

// The bar we have to meet to trigger trimming measured by free+standby count
#define TRIM_LOW_BAR 400
#define EMERGENCY_LOW_BAR 120
// How many pages to trim per batch
#define TRIM_BATCH_SIZE 240
// Number of modified pages we want before writing to disk
// Number can't exceed the disk size
# define WRITE_BATCH_SIZE 240

#define MAX_DISC_PTE_BITS 40

#define MAX_DISC_SIZE ((ULONG64) 1 << MAX_DISC_PTE_BITS)
// Define thresholds for the bar for low memory

#define NUM_THREADS 4

#define TICK_MS 500
#define AGE_TICK_MS 250
#define CHUNKS_PER_PAGE (PAGE_SIZE / sizeof(ULONG_PTR))



// List Head Primitives to build Linked Lists
// Functions include: initialize, isListEmpty, insert, remove
// typedef struct _LIST_ENTRY {
//     struct _LIST_ENTRY *Flink;
//     struct _LIST_ENTRY *Blink;
// } LIST_ENTRY, *PLIST_ENTRY;


#define DEBUG 1
#if DEBUG
#define ASSERT(x) {if(!(x)) DebugBreak();}
#else
#define ASSERT(x)
#endif
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
    PLIST_ENTRY Flink;
    PLIST_ENTRY Entry;

    //
    // Remove the entry currently at the head of the list.
    //

    Entry = ListHead->Flink;
    Flink = Entry->Flink;
    ListHead->Flink = Flink;
    Flink->Blink = ListHead;

    return Entry;
}

// TODO: a little confused
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

// Borrowed Noah's random function
typedef struct {
    ULONG_PTR state;
    ULONG_PTR counter;
} THREAD_RNG_STATE;

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


//
// This define enables code that lets us create multiple virtual address
// mappings to a single physical page.  We only/need want this if/when we
// start using reference counts to avoid holding locks while performing
// pagefile I/Os - because otherwise disallowing this makes it easier to
// detect and fix unintended failures to unmap virtual addresses properly.
//

#define SUPPORT_MULTIPLE_VA_TO_SAME_PAGE 1
#pragma comment(lib, "advapi32.lib")

// If multiple VA, it links to onecore.lib
#if SUPPORT_MULTIPLE_VA_TO_SAME_PAGE
#pragma comment(lib, "onecore.lib")
#endif

#define PAGE_SIZE                   4096

#define KB(x)                       ((x) * 1024)
// Converts megabytes to bytes
#define MB(x)                       ((x) * 1024 * 1024)

//
// This is intentionally a power of two so we can use masking to stay
// within bounds.
//

// Events
HANDLE trim_needed;
HANDLE write_needed;
HANDLE pages_available;

// Locks for multi-threading
CRITICAL_SECTION disc_lock;

ULONG_PTR virtual_address_size_in_unsigned_chunks;

// __declspec(thread) keyword: allows each thread to have it's own private copy of the below variables that act as globals
__declspec(thread) int my_hard_faults = 0;
__declspec(thread) int my_soft_faults = 0;
__declspec(thread) int my_repurpose_faults = 0;

__declspec(thread) int my_thread_id = -1;

// Struct for the number of different faults per thread
typedef struct {
    int thread_id;
    int hard_faults;
    int soft_faults;
    int repurpose_faults;
    int total_accesses;
    double elapsed_ms;
} ThreadStats;

// Create an array that demonstrates each thread's final results
ThreadStats final_results[NUM_THREADS];

// PTE Struct with three states: valid, transition, and disc
typedef struct {
    ULONG_PTR valid: 1;
    ULONG_PTR frame_number: 40;
    ULONG_PTR age: 3;
    ULONG_PTR reserved: 20;
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
        // Include a pointer to the entire PTE; TODO/
        ULONG_PTR entire;
    };
} PTE, *PPTE;

// Create a global variable of an array of PTEs
PPTE page_table;
// Pointer to the start of the space allocated for the user virtual address
PULONG_PTR va_space_start;

// Pointer to the start of the space allocated for the system virtual address
// Used specifically for mapping page's data to the disk
PULONG_PTR system_va_start;

// Define our PTE section struct
# define NUM_PTE_SECTIONS 64
#define NUM_AGES 8

typedef struct {
    CRITICAL_SECTION lock;
    LIST_ENTRY age_lists[NUM_AGES];
    int age_counts[NUM_AGES];
} PTE_SECTION;

// Pointer to the array of pte_sections
PTE_SECTION *pte_sections;
// Number of PTEs per section
ULONG_PTR ptes_per_section;

// Increment age unless age equals the maximum of 7; therefore, it remains the same
VOID
increment_pte_age(PVALID_PTE pte) {
    if (pte->age < 7) {
        pte->age++;
    }
}

// Define the List Header Struct
typedef struct {
    LIST_ENTRY entry;
    CRITICAL_SECTION lock;
    ULONG_PTR size;
} LIST_HEAD, * PLIST_HEAD;

// Custom struct for our PFNs
typedef struct {
    LIST_ENTRY list;
    PPTE pte;
    ULONG_PTR disc_index: MAX_DISC_PTE_BITS;
    ULONG_PTR isOccupied: 2; // 00-free, 01-active, 10-modified, 11-standby
    ULONG_PTR write_in_progress: 1; // 1: being written to disc
} pfn_metadata;

VOID
initialize_list_head(PLIST_HEAD head) {
    InitializeListHead(&head->entry);
    InitializeCriticalSectionAndSpinCount(&head->lock, 0x00FFFFFF);
    head->size = 0;

    return;
}

// Sets up global variable that points to the allocated memory of the pfn_metadata
pfn_metadata * pfn_table;
// Sets up global variable that stores the value of the biggest frame number magnitude
ULONG_PTR max_frame_number = 0;
PULONG_PTR physical_page_numbers;

// Global Lists to keep track of the PFNs in each state
LIST_HEAD pfn_free_list;
LIST_HEAD pfn_modified_list;
LIST_HEAD pfn_standby_list;

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

// Calculate the pointer to the specific PTE
PPTE
get_pte_from_va(PULONG_PTR arbitrary_va) {
    ULONG_PTR index = ((ULONG_PTR) arbitrary_va - (ULONG_PTR) va_space_start) / PAGE_SIZE;
    return page_table + index;
}

// Return the virtual address based on a pointer to it's corresponding pte
PULONG_PTR
get_va_from_pte(PPTE pte) {
    ULONG_PTR pte_index = pte - page_table;
    // Reverse the math for calculating the index in the get_pte_from_va
    return va_space_start + (pte_index * (PAGE_SIZE / sizeof (ULONG_PTR)));
}

// Return the PTE section the inputted pte belongs to
PTE_SECTION *
    get_section(PPTE pte) {
    ULONG_PTR index = (ULONG_PTR) (pte - page_table);
    return &pte_sections[index/ptes_per_section];
}
// TODO: need a lock!
// Set the frame number to the physical page, set valid bit to 1, and update aging lists and counter
// Need to be called with a lock
VOID
set_pte_valid(PTE_SECTION * section, pfn_metadata * meta, PPTE pte, ULONG_PTR pfn) {
    // Set new frame number
    pte->hardware.frame_number = pfn;
    // Reset hardware age
    pte->hardware.age = 0;
    pte->hardware.valid = TRUE;

    // Add pte into the smallest, hottest age list (i.e. 0) in its corresponding section
    InsertTailList(&section->age_lists[0], &meta->list);
    // Update the PTE section's age count
    section->age_counts[0]++;
}

// Set valid bit to 0, but leave the physical page linked in case we want to retrieve the data
VOID
set_pte_invalid(PPTE pte) {
    pte->hardware.valid = FALSE;
}

ULONG_PTR disc_page_count = NUM_DISC_PAGES;
int filled_disc_slots = 0;

// Initialize disc
PVOID official_disc;

typedef struct _DISC_SLOT_ENTRY {
    LIST_ENTRY list;
    ULONG_PTR  slot_index;
    BOOLEAN valid;
} DISC_SLOT_ENTRY, *PDISC_SLOT_ENTRY;

// Pointer to the memory resevered for the disk
DISC_SLOT_ENTRY * disc_slot_entry;
// Free list of disc slots
LIST_ENTRY disc_free_list;

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
    EnterCriticalSection(&disc_lock);

    if (slot >= disc_page_count) {
        printf("Disk slot out of bounds\n");
        DebugBreak();
        LeaveCriticalSection(&disc_lock);
        return;
    }

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

// TODO; remove pfn field from frame number
// TODO: watch out for straddling
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

        InsertTailList(&pfn_free_list.entry, &pfn->list);
        pfn_free_list.size++;
    }
}

pfn_metadata*
    find_pfn_from_frame_number(ULONG_PTR frame_number) {
    return &pfn_table[frame_number];
}
// Based on the address of a pfn_entry, find the frame number using pointer arithmetic
ULONG_PTR
find_frame_number_from_pfn(pfn_metadata *pfn) {
    ASSERT(pfn > pfn_table);
    return (ULONG_PTR) (pfn - pfn_table);
}

VOID
age_pages() {
    // Iterate through each PTE section
    for (int i = 0; i < NUM_PTE_SECTIONS; i++) {
        PTE_SECTION * section =  &pte_sections[i];
        EnterCriticalSection(&section->lock);

        // Iterate through the PTE's in each section from oldest to youngest
        // Except skip the oldest age bucket, which stands as the ceiling
        for (int age = NUM_AGES - 2; age >= 0; age--) {
            // Iterate through each page in the age bucket and age each PTE by one
            while (section->age_counts[age] > 0) {
                // Remove and store the pointer to the head of the current age linked list
                pfn_metadata * pfn = (pfn_metadata *) RemoveHeadList(&section->age_lists[age]);
                section->age_counts[age]--;

                // Update the PTE age field by one
                pfn->pte->hardware.age = age + 1;

                // Put it into the next age bucket (age + 1)
                InsertTailList(&section->age_lists[age + 1], &pfn->list);
                section->age_counts[age + 1]++;
            }
        }

        LeaveCriticalSection(&section->lock);
    }
}


DWORD WINAPI
age_thread () {
    while (TRUE) {
        // Triggered by shut down or the routine time check
        DWORD r = WaitForSingleObject(shutdown_event, AGE_TICK_MS);

        // If shutdown, exit
        if (r == WAIT_OBJECT_0) {
            break;
        }

        // Do an aging pass
        age_pages();
    }
}

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

// TODO: is this the whole array idea?
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

// Number of physical pages that are actually allocated by the OS
ULONG_PTR physical_page_count = NUMBER_OF_PHYSICAL_PAGES;

// TODO: if more than num percent of overall pages on standby pages
// TODO: if 90 percent, trim
// TODO: if not, don't trim
// Create a event
// TODO: every now and then, need the get_free_pages to wait and not get in the way of trim_pages
VOID
trim_pages () {
    EnterCriticalSection(&pfn_free_list.lock);
    EnterCriticalSection(&pfn_standby_list.lock);

    // TODO: SEVERAL LOCKS
    // If trim pages was unneccarily called, check if it is low or now
    if (pfn_standby_list.size + pfn_free_list.size >= TRIM_BATCH_SIZE) {
        SetEvent(pages_available);
        LeaveCriticalSection(&pfn_free_list.lock);
        LeaveCriticalSection(&pfn_standby_list.lock);
        return;
    }

    LeaveCriticalSection(&pfn_free_list.lock);
    LeaveCriticalSection(&pfn_standby_list.lock);

    int total_pages_trimmed = 0;
    int curr_section = 0;

    // Iterate through each section, holding its own lock at a time
    while (curr_section < NUM_PTE_SECTIONS && total_pages_trimmed < TRIM_BATCH_SIZE) {
        // Find pointer to curr_section
        PTE_SECTION * section = &pte_sections[curr_section];
        // Initialize array to keep track of pages to trim
        pfn_metadata * trimmed_pages[TRIM_BATCH_SIZE];
        PVOID trimmed_pages_vas[TRIM_BATCH_SIZE];
        // Keep track of how many pages trimmed in this section
        int sec_pages_trimmed = 0;

        EnterCriticalSection(&section->lock);

        // Iterate through the age lists by oldest/coldest page to youngest/hottest page
        int age = NUM_AGES - 1;
        // Continue iterating while the number of pages trimmed is less than the batch size
        while (age >= 0 && total_pages_trimmed + sec_pages_trimmed < TRIM_BATCH_SIZE) {
            // Iterate through the current oldest age bucket and pop off as many pages as needed
            while (section->age_counts[age] > 0 && total_pages_trimmed + sec_pages_trimmed < TRIM_BATCH_SIZE) {
                // Pop the head of the current age list and store the pointer to its pfn_metadata
                pfn_metadata * trim_page = (pfn_metadata *) RemoveHeadList(&section->age_lists[age]);

                // Update the counter on the number of items of the linked list
                section->age_counts[age]--;

                // Get the pte of the trimmed page and store it in the array of trimmed page's VA
                PPTE pte = trim_page->pte;
                trimmed_pages_vas[sec_pages_trimmed] = get_va_from_pte(pte);

                // Invalidate the pte
                set_pte_invalid(pte);
                // Update the pte to transition state
                pte->transition.transition = 1;
                pte->transition.frame_number = find_frame_number_from_pfn(trim_page);

                trimmed_pages[sec_pages_trimmed] = trim_page;

                sec_pages_trimmed++;
            }

            // Move on to the next largest age
            age--;
        }

        if (sec_pages_trimmed > 0) {
            // While holding the section's lock, unmap the batch before another thread accesses it
            if (MapUserPhysicalPagesScatter(trimmed_pages_vas, sec_pages_trimmed, NULL) == FALSE) {
                printf("TRIM_PAGES: scatter unmapped failed\n");
            }

            // Update the trimmed_pages to the modified status
            EnterCriticalSection(&pfn_modified_list.lock);
            for (int i = 0; i < sec_pages_trimmed; i++) {
                trimmed_pages[i]->isOccupied = 2;
                // Add onto modified list
                InsertTailList(&pfn_modified_list.entry, &trimmed_pages[i]->list);
                pfn_modified_list.size++;
            }
            LeaveCriticalSection(&pfn_modified_list.lock);

            total_pages_trimmed += sec_pages_trimmed;
        }

        LeaveCriticalSection(&section->lock);

        curr_section++;
    }
}

VOID
write_to_disk (int count, pfn_metadata ** pages_to_write, ULONG_PTR * disc_slots) {
    // Arrays for scatter mapping
    PVOID va_array[WRITE_BATCH_SIZE];
    ULONG_PTR pfn_array[WRITE_BATCH_SIZE];
    for (int i = 0; i < count; i += 1) {
        // Get the pointer of the pfn metadata which is the same as the address of entry
        // As in pte metadata struct, entry is intentionally the first entry of pfn metadata
        pfn_metadata* meta = pages_to_write[i];

        // Get the frame number of physical page
        ULONG_PTR pfn = find_frame_number_from_pfn(meta);

        // Create a slot (one only per entry) in the system VA space
        PULONG_PTR sys_slot = (PULONG_PTR)((PBYTE)system_va_start + i * PAGE_SIZE);

        va_array[i] = (PVOID)sys_slot;
        pfn_array[i] = pfn;
    }

    // Map multiple pages to sys VA at once for efficiency
    if (MapUserPhysicalPagesScatter(va_array, count, pfn_array) == FALSE) {
        printf("WRITE TO DISK: Scatter map failed, count=%d\n", count);
        return;
    }

    // Copy the data from the page to the disk
    for (int i = 0; i < count; i += 1) {
        pfn_metadata *meta = pages_to_write[i];
        PULONG_PTR sys_slot = (PULONG_PTR)va_array[i];

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
        memcpy((char*)official_disc + disc_slot * PAGE_SIZE, sys_slot, PAGE_SIZE);

        // Save the free disc slot into an array
        disc_slots[i] = disc_slot;
    }
}
//
pfn_metadata *
get_free_page() {
    // Claim the lock
    EnterCriticalSection(&pfn_free_list.lock);

    // Free: If free list is not empty, take a free page
    if (IsListEmpty(&pfn_free_list.entry) == FALSE) {
        // Move the list from the free list
        PLIST_ENTRY entry = RemoveHeadList(&pfn_free_list.entry);
        pfn_free_list.size--;

        pfn_metadata *meta = (pfn_metadata *) entry;

        // Set the page metadata to isOccupied
        meta->isOccupied = 1;

        // Release the lock after finishing
        LeaveCriticalSection(&pfn_free_list.lock);

        return meta;
    }
    LeaveCriticalSection(&pfn_free_list.lock);

    EnterCriticalSection(&pfn_standby_list.lock);
    // Standby: Repurpose the page from the standby list
    if (IsListEmpty(&pfn_standby_list.entry) == FALSE) {
        // Move the list from the standby list
        PLIST_ENTRY entry = RemoveHeadList(&pfn_standby_list.entry);
        pfn_standby_list.size--;

        pfn_metadata *meta = (pfn_metadata *) entry;

        // Get the address of the pte related to the data in the standby page
        PPTE old_pte = meta->pte;
        // Copy the contents of the pte
        PTE new_pte_contents = * old_pte;

        // Update the contents of the copied pte
        // Mark as invalid, not transition, in disc, and change PFN to disk address
        new_pte_contents.disc.valid = 0;
        new_pte_contents.disc.transition = 0;
        new_pte_contents.disc.disc = 1;
        new_pte_contents.disc.disc_index = meta->disc_index;

        // Update the contents of the pte
        old_pte->entire = new_pte_contents.entire;

        my_repurpose_faults++;

        // Set the page metadata to isOccupied
        meta->isOccupied = 1;

        // Release the lock after finishing
        LeaveCriticalSection(&pfn_standby_list.lock);

        return meta;
    }

    LeaveCriticalSection(&pfn_standby_list.lock);

    // Reset the event
    ResetEvent(pages_available);
    // If there are none, we must trim the pages by triggering the event
    SetEvent(trim_needed);

    // Get free page failed
    // printf("GET_FREE_PAGE free=0, standby=0. We want to trim and then most likely need to write too (free=%d, standby=%d, modified=%d)\n",
    //        pfn_free_list.size, pfn_standby_list.size, pfn_modified_list.size);
    // Even if error, ensure the pfn lock can be released
    return NULL;
}

VOID
activate_page (PPTE pte, pfn_metadata *meta, ULONG_PTR pfn) {
    // Point physical page back to PTE
    meta->pte = pte;
    // Update pfn status
    meta->isOccupied = 1;
}

// Trim thread triggered in two ways
// preemptive: TIMEOUT fires, checking if there is a need to trim before a page experiences no free pages
// emergency: preemptive trim failed, meaning a worker failed to find a free page
DWORD WINAPI
trim_thread (LPVOID lpParam) {
    HANDLE wake[2] = { trim_needed, shutdown_event };

    while (TRUE) {
        // Either triggered by the TIMEOUT or the trim_needed event
        DWORD r = WaitForMultipleObjects(2, wake, FALSE, TICK_MS);

        // Check if the trigger was a shutdown event
        if (r == WAIT_OBJECT_0 + 1) {
            break;
        }

        // Mark if the trigger was a trim_needed event from a worker thread
        BOOL emergency = (r == WAIT_OBJECT_0);

        // Find the counts of each list to dictate the trimming/writing decisions
        int modified = pfn_modified_list.size;
        int free = pfn_free_list.size;
        int standby = pfn_standby_list.size;

        // If there are sufficient modified pages, there is no need to unmap more physical pages
        // Instead to get free pages, only need to write the data to disk
        if (modified >= WRITE_BATCH_SIZE) {
            SetEvent(write_needed);
        }
        // If there are insufficient free and modified pages, trim first to build a batch then write to trim
        else if (free + standby < TRIM_LOW_BAR) {
            trim_pages();
            SetEvent(write_needed);
        }
    }
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

        // Check if write to disk should be performed based on two reasons
        // 1. if there is a full batch of modified pages queued
        // 2. if the free page supply is nearly depleted and there are some modified pages to write
        int modified, supply;

        // Access the counts of free, modified, and standby list without lock
        // The choice of no locks were intentional as we only want a snapshot or rough estimate to guide our logic flow
        modified = pfn_modified_list.size;
        supply = pfn_free_list.size + pfn_standby_list.size;

        BOOL full_batch = (modified >= WRITE_BATCH_SIZE);
        BOOL emergency = (supply < EMERGENCY_LOW_BAR && modified > 0);

        if (!full_batch && !emergency) {
            if (supply > 0) {
                SetEvent (pages_available);
            }
            continue;
        }

        // Check to make sure if modified list has entries to write to disk
        if (IsListEmpty(&pfn_modified_list.entry)) {
            // If there are available pages, make sure to set event
            EnterCriticalSection (&pfn_free_list.lock);
            EnterCriticalSection (&pfn_standby_list.lock);
            if (pfn_free_list.size + pfn_standby_list.size > 0) {
                SetEvent(pages_available);
            }
            LeaveCriticalSection (&pfn_free_list.lock);
            LeaveCriticalSection(&pfn_standby_list.lock);
            continue;
        }

        // Find how many avilable disc slots based on counter of filled pages
        // No need for lock as the worker threads that have access to these variables would only increase the amount
        // of empty slots; therefore, the number of available disc slots would increase which does not affect our code
        int available_disc_slots =  disc_page_count - filled_disc_slots;

        // If there are no empty slots
        if (available_disc_slots <= 0) {
            // Disk is full so pages cannot go from modified to disk
            // printf("WRITE THREAD: Disk is full\n");

            EnterCriticalSection (&pfn_free_list.lock);
            EnterCriticalSection (&pfn_standby_list.lock);
            // Check if there are free pages or standby list pages
            if (pfn_standby_list.size > 0 || pfn_free_list.size > 0) {
                // Allow repurpose and signal finished of trim and write thread
                SetEvent(pages_available);
            }

            LeaveCriticalSection(&pfn_free_list.lock);
            LeaveCriticalSection(&pfn_standby_list.lock);

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

            pfn_metadata * meta = (pfn_metadata *) current;
            // Mark as writing to disk
            meta->write_in_progress = 1;

            // Add to the array of pages to write to disk and increment counter
            pages_to_write[num_pages_written++] = meta;

            // Set next page
            current = current->Flink;
        }

        LeaveCriticalSection (&pfn_modified_list.lock);

        ULONG_PTR disc_slots[WRITE_BATCH_SIZE];
        // Set every value in disk slot to -1 to ensure we can precisely identify a failed map later on
        for (int i = 0; i < WRITE_BATCH_SIZE; i++) {
            disc_slots[i] = (ULONG_PTR) -1;
        }

        write_to_disk(num_pages_written, pages_to_write, disc_slots);

        EnterCriticalSection(&pfn_modified_list.lock);
        EnterCriticalSection(&pfn_standby_list.lock);

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

            // Check if the write in progress is invalid, meaning there was a soft fault mid-write
            if (meta->write_in_progress == 0) {
                // Disc slot no longer needed and stale
                empty_disc_slot(slot);
                continue;
            }
            // Or the page was successfully written in disk and now trasitioning to disk state
            // Write data to disk
            meta->disc_index = slot;

            RemoveEntryList (&meta->list);
            pfn_modified_list.size--;

            meta->write_in_progress = 0;
            // In disk state
            meta->isOccupied = 3;

            // Move the page into the disk linked list and update counter
            InsertTailList (&pfn_standby_list.entry, &meta->list);
            pfn_standby_list.size++;
        }
        LeaveCriticalSection(&pfn_modified_list.lock);
        LeaveCriticalSection(&pfn_standby_list.lock);

        // Tell the get_free_page function that there are now standby lists ready if needed
        SetEvent(pages_available);
    }

    return 0;
}

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

    initialize_list_head(&pfn_free_list);
    initialize_list_head(&pfn_modified_list);
    initialize_list_head(&pfn_standby_list);

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

        printf ("full_virtual_memory_test : allocated only %llu pages out of %u pages requested\n",
                physical_page_count,
                NUMBER_OF_PHYSICAL_PAGES);

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

    va_space_start = VirtualAlloc2 (NULL,
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
#else

    va_space_start = VirtualAlloc (NULL,
                      virtual_address_size,
                      MEM_RESERVE | MEM_PHYSICAL,
                      PAGE_READWRITE);

#endif

    if (va_space_start == NULL) {

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
    // TODO: wtf is this calculation
    ptes_per_section = (num_ptes + NUM_PTE_SECTIONS - 1) / NUM_PTE_SECTIONS;

    // If integer division results in 0, only enough PTE's for one section
    if (ptes_per_section == 0) {
        ptes_per_section = 1;
    }

    // TODO: is this what it even means. Allocate memory for our pte_sections
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

// Calling function needs to hold the pte_lock
BOOL
handle_soft_fault(PPTE pte, PULONG_PTR aligned_va, pfn_metadata ** official_meta, ULONG_PTR * official_pfn) {
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

    // Need the pfn_lock if editing the data
    EnterCriticalSection (&pfn_modified_list.lock);
    EnterCriticalSection (&pfn_standby_list.lock);


    // Check the status of the PTE after releasing the lock as another thread could have altered its contents/status
    // If the PTE does not satisfy a soft fault, return to full_virtual memory to force another page fault and reroute
    // to correct function that can resolve the fault
    if (pte->transition.transition != 1 || pte->transition.frame_number != pfn) {
        LeaveCriticalSection (&pfn_modified_list.lock);
        LeaveCriticalSection (&pfn_standby_list.lock);
        return FALSE;
    }

    // If the disc is currently being written to disc
    if (meta->write_in_progress == 1) {
        // Can update the write_in_progress bit about the soft fault so the disc can later empty the stale slot
        meta->write_in_progress = 0;
    }

    // Check if page is in standby used to trigger empty disc slot after releasing the lock
    BOOL was_standby = (meta->isOccupied == 3);

    // Remove the page from the standby/modified list
    RemoveEntryList (&meta->list);

    // Update the counter
    if (was_standby) {
        pfn_standby_list.size--;
    } else {
        // There was a soft fault from the modified list count
        pfn_modified_list.size--;
    }
    // Release the lock
    LeaveCriticalSection (&pfn_modified_list.lock);
    LeaveCriticalSection (&pfn_standby_list.lock);

    // Update the pfn metadata; Lock-free because the page is removed from global lists and isOccupied is already TRUE
    activate_page(pte, meta, pfn);

    // Is it not already mapped; therefore, we only need to mark it as valid?
    if (MapUserPhysicalPages (aligned_va, 1, &pfn) == FALSE) {
        printf ("soft fault: full_virtual_memory_test : could not map VA %p to page %llX\n", aligned_va, pfn);
        DebugBreak();
        return FALSE;
    }

    // printf("[Thread %d] SOFT FAULT mapped VA %p to PFN %llx\n", my_thread_id, (void*)aligned_va, (unsigned long long)pfn);

    if (was_standby) {
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
    PTE_SECTION *section = get_section(pte);

    // This a zero PTE or a disc PTE
    // Find a free physical page in memory
    pfn_metadata * meta = get_free_page();

    // If we return NULL, we did not get a free page so try again
    while (meta == NULL) {
        // Release the pte section lock to allow the trim thread to work
        LeaveCriticalSection(&section->lock);

        // Wait for the trim to finish
        WaitForSingleObject(pages_available, INFINITE);

        EnterCriticalSection(&section->lock);

        // Check if another thread edited the pte to be valid or in transition. If so, punt
        if (pte->hardware.valid == 1 || pte->transition.transition == 1) {
            return FALSE;
        }

        meta = get_free_page();

    }

    // Get the frame number of the free page using pointer arithmetic
    ULONG_PTR pfn = find_frame_number_from_pfn((meta));

    // Update the pfn metadata; Lock-free because the page is removed from global lists and isOccupied is already TRUE
    activate_page(pte, meta, pfn);

    // Map the VA to the physical page
    if (MapUserPhysicalPages (aligned_va, 1, &pfn) == FALSE) {
        printf ("hard fault: full_virtual_memory_test : could not map VA %p to page %llX\n", aligned_va, pfn);
        DebugBreak();
        return FALSE;
    }

    // printf("[Thread %d] HARD FAULT mapped VA %p to PFN %llx\n", my_thread_id, (void*)aligned_va, (unsigned long long)pfn);

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

        // Use memcpy to copy the data in the disc into the free page in memory
        memcpy(aligned_va, (char*)official_disc + slot_on_disc * PAGE_SIZE, PAGE_SIZE);
        // Mark the data in the disc slot to be invalid
        empty_disc_slot(slot_on_disc);
    } else {
        // Zeros the page if new
        memset(aligned_va, 0, PAGE_SIZE);
    }
    // // Increment the age bit of the rest of the pages
    // // TODO: find better solutions for aging
    // // TODO: do an if, if number of free pages (using a counter), is less than half of physical memory then do this
    // // TODO: keep a count of free_pages,
    // for (int j = 0; j < NUMBER_OF_PHYSICAL_PAGES; j += 1) {
    //     if (pfn_table[j].pte != NULL && pfn_table[j].pte != pte) {
    //         increment_pte_age(pfn_table[j].pte);
    //     }
    // }
    // // TODO: break into functions
    my_hard_faults++;

    *official_meta = meta;
    *official_pfn = pfn;

    return TRUE;
}

VOID
check_accuracy() {
    printf("Checking the accuracy of stamped data\n");
    int num_corrupt_page = 0;
    int bad_valid = 0, bad_trans = 0, bad_disc = 0;

    for (int i = 0; i < virtual_address_size_in_unsigned_chunks; i += CHUNKS_PER_PAGE) {
        // Get my VA
        PULONG_PTR va = va_space_start + i;
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
    VirtualFree (va_space_start, 0, MEM_RELEASE);
    VirtualFree(system_va_start, 0, MEM_RELEASE);
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
            // Get a random number

            random_number = GetNextRandom(&my_rng);
            random_number %= virtual_address_size_in_unsigned_chunks;

            //
            // Ensure the write to the arbitrary virtual address doesn't
            // straddle a PAGE_SIZE boundary just to keep things simple for
            // now.
            //

            // Clears the low 3 bits, ensure random address is 8 byte (previously ~0x7)
            // Align at the start of the page so arbitrary_va is the first chunk of the page
            random_number &= ~(CHUNKS_PER_PAGE - 1);
            // Set up mock virtual address
            arbitrary_va = va_space_start + random_number;
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
                fault_resolution = handle_soft_fault(pte, aligned_va, &meta, &pfn);
            }
            // DISK FAULT or ZERO FAULT
            // TODO: udpate comment If we are in disk or if we are in modified/standby
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

            // DOn't advance to the next VA, retry to make sure successful stamping of data
            fault_resolution = FALSE;
            if (i % 100000 == 0) printf(".");
        }
    }

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
    pages_available = CreateEvent(NULL, TRUE, FALSE, NULL);

    // Trim Thread
    HANDLE trim_thread_handle  = CreateThread(NULL, 0, trim_thread, NULL, 0, NULL);

    HANDLE write_thread_handle = CreateThread(NULL, 0, write_thread, NULL, 0, NULL);

    HANDLE age_thread_handle = CreateThread(NULL, 0, age_thread, NULL, 0, NULL);

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

    // TODO: create individual thread for trimming

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

    check_accuracy();

    // Close all thread handles and print out their stats
    for (int i = 0; i < NUM_THREADS; i++) {
        CloseHandle(thread_handles[i]);
    }

    // Close background thread handles
    CloseHandle(trim_thread_handle);
    CloseHandle(write_thread_handle);
    CloseHandle(age_thread_handle);

    // Close events
    CloseHandle(trim_needed);
    CloseHandle(write_needed);
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

    printf("Total time for all workload: %.2f ms\n", total_ms);
    printf("Total accesses across threads: %lld\n", all_accesses);

    cleanup();
    return 0;
}

// TODO:
// Create a free list of empty disc slot
    // a) create a disc struct and linked list
    // b) create a stack struct and use a stack that tracks the indicies of empty disc array slots
// Create target functions
    // create target functions
    // faulting and the arbitrary function
    // aging
    // trimming
    // modified page writing

// TODO: DISK
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

// TODO: QUESTIONS
// currently, pte_lock is global, how do i resolve it?
    // I was thinking about doing a lock for each virtual address, and with some research, i found a hashing strategy
    // what are some other technique
// add a disc lock

// wait what happens with a lock inside a lock wtf

// for getting a free disk page: which currently uses an array of booleans as its metadata
    // should i create a stack of free indicies
    // should i create a struct and linked list (will we add anything later on?)
// ask about the trim_pages with one thread
