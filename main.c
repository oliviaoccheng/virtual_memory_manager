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

// Performance trace cheat sheet
// xperf -on base -stackwalk profile
// Then run your program
// xperf -stop -d trace1.etl
// trace1.etl
// Once in the trace, click Trace and then Load Symbols

#include <stdio.h>
#include <stdlib.h>
#include <windows.h>

// List Head Primitives to build Linked Lists
// Functions include: initialize, isListEmpty, insert, remove

// typedef struct _LIST_ENTRY {
//     struct _LIST_ENTRY *Flink;
//     struct _LIST_ENTRY *Blink;
// } LIST_ENTRY, *PLIST_ENTRY;

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

//
// This define enables code that lets us create multiple virtual address
// mappings to a single physical page.  We only/need want this if/when we
// start using reference counts to avoid holding locks while performing
// pagefile I/Os - because otherwise disallowing this makes it easier to
// detect and fix unintended failures to unmap virtual addresses properly.
//

#define SUPPORT_MULTIPLE_VA_TO_SAME_PAGE 0
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

#define NUMBER_OF_PHYSICAL_PAGES 1000 // ((VIRTUAL_ADDRESS_SIZE / PAGE_SIZE) / 64)
#define NUM_DISC_PAGES MB(2)
#define MAX_DISC_PTE_BITS 40
#define MAX_DISC_SIZE ((ULONG64) 1 << MAX_DISC_PTE_BITS)

ULONG_PTR virtual_address_size_in_unsigned_chunks;

int num_soft_fault = 0;
int num_hard_fault = 0;
int num_repurpose = 0;


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
    };
} PTE, *PPTE;

// Create a global variable of an array of PTEs
PPTE page_table;
// Pointer to the start of the space allocated for the virtual address
PULONG_PTR va_space_start;

// Increment age unless age equals the maximum of 7; therefore, it remains the same
VOID
increment_pte_age(PVALID_PTE pte) {
    if (pte->age < 7) {
        pte->age++;
    }
}

// Custom struct for our PFNs
typedef struct {
    LIST_ENTRY list;
    PPTE pte;
    ULONG_PTR disc_index: MAX_DISC_PTE_BITS;
    ULONG_PTR isOccupied: 2; // 00-free, 01-active, 10-modified, 11-standby
} pfn_metadata;

// Sets up global variable that points to the allocated memory of the pfn_metadata
pfn_metadata * pfn_table;
// Sets up global variable that stores the value of the biggest frame number magnitude
ULONG_PTR max_frame_number = 0;
PULONG_PTR physical_page_numbers;

// Global Lists to keep track of the PFNs in each state
LIST_ENTRY pfn_free_list;
LIST_ENTRY pfn_modified_list;
LIST_ENTRY pfn_standby_list;

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

// Set the frame number to the physical page and set valid bit to 1
VOID
set_pte_valid(PPTE pte, ULONG_PTR frame_number) {
    pte->hardware.frame_number = frame_number;
    pte->hardware.valid = TRUE;
}

// Set valid bit to 0, but leave the physical page linked in case we want to retrieve the data
VOID
set_pte_invalid(PPTE pte) {
    pte->hardware.valid = FALSE;
}

ULONG_PTR disc_page_count = NUM_DISC_PAGES;

// Disk Metadata: simply an array for now, later, can change to struct
PBOOLEAN disc_metadata;

// Initialize disc
PVOID page_file_base;

PVOID
create_page_file() {
    // Number_of_pages restricted by the disc size
    if (disc_page_count > MAX_DISC_SIZE) {
        disc_page_count = MAX_DISC_SIZE;
    }

    // In the memory, reserve pages for our fake disc
    ULONG_PTR num_bytes = disc_page_count * PAGE_SIZE;
    page_file_base = malloc(num_bytes);

    // If the malloc fails, attempt to allocate half the memory size until it succeds
    while (page_file_base == NULL) {
        num_bytes /= 2;
        page_file_base = malloc (num_bytes);
    }

    disc_page_count = num_bytes / PAGE_SIZE;
    //
    disc_metadata = zero_malloc(disc_page_count);
    return page_file_base;
}

// Iterate through the disc to find a empty page based on the disc metadata
int find_free_disc_slot() {
    for (int i = 0; i < disc_page_count; i++) {
        if (disc_metadata[i] == FALSE) {
            disc_metadata[i] = TRUE;
            return i;
        }
    }
    return -1;
}

// Free disc space
VOID
empty_disc_slot(ULONG_PTR slot) {
    if (slot >= disc_page_count) {
        printf("Disk slot out of bounds");
        DebugBreak();
        return;
    }

    if (disc_metadata[slot] == FALSE) {
        printf("Disc metadata twice FALSE");
        DebugBreak();
        return;
    }

    disc_metadata[slot] = FALSE;
}

// TODO; remove pfn field from frame number
// TODO: watch out for straddling
VOID
setup_pfn_table() {
    // Find the maximum frame number by iterating through every physical page
    max_frame_number = 0;
    for (int i = 0; i < NUMBER_OF_PHYSICAL_PAGES; i++) {
        if (max_frame_number < physical_page_numbers[i]) {
            max_frame_number = physical_page_numbers[i];
        }
    }

    // Reserve memory for our pfn_table
    ULONG_PTR pfn_table_size = (max_frame_number + 1) * sizeof(pfn_metadata);
    pfn_table = VirtualAlloc(NULL, pfn_table_size, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);

    // Check if the memory was successfully reserved
    if (pfn_table == NULL) {
        printf("Failed to reserve memory for the pfn_table/pfn metadata");
        DebugBreak();
        return;
    }

    // Commit the real physical pages by iterating through the physical page numbers and committing them
    for (int i = 0; i < NUMBER_OF_PHYSICAL_PAGES; i++) {
        // Find the frame number
        ULONG_PTR frame_number = physical_page_numbers[i];
        // Find the address of the corresponding pfn_table entry of that frame_number
        pfn_metadata * pfn = &pfn_table[frame_number];

        memset(pfn, 0, sizeof(pfn_metadata));
        InsertTailList(&pfn_free_list, &pfn->list);
    }
}

pfn_metadata*
    find_pfn_from_frame_number(ULONG_PTR frame_number) {
    return &pfn_table[frame_number];
}
// Based on the address of a pfn_entry, find the frame number using pointer arithmetic
ULONG_PTR
find_frame_number_from_pfn(pfn_metadata *pfn) {
    return (ULONG_PTR) (pfn - pfn_table);
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

//
void
trim_pages (int batch_size, ULONG_PTR count) {
    PULONG_PTR va_to_unmap[NUMBER_OF_PHYSICAL_PAGES / 2];
    int num_pages_unmapped = 0;

    // Iterate through the pfn_metadata to find the oldest pages
    while (num_pages_unmapped < batch_size) {
        pfn_metadata * oldest_pfn = NULL;
        ULONG_PTR highest_age = 0;

        // TODO: create a local array of age counts of 8, set everything to 0
        // TODO: index (age) and contents (num of that age)
        // TODO: make global variable and make function to keep update
        for (int j = 0; j < NUMBER_OF_PHYSICAL_PAGES; j += 1) {
            ULONG_PTR frame_number = physical_page_numbers[j];
            // Get the physical metadata frame number's address
            pfn_metadata * pfn = &pfn_table[frame_number];

            if (pfn->isOccupied == 1 && pfn->pte->hardware.age >= highest_age) {
                highest_age = pfn->pte->hardware.age;
                oldest_pfn = pfn;
            }
        }

        // Edge Case: if nothing is found
        if (oldest_pfn == NULL) {
            break;
        }

        // TODO: ideas for efficiency--not n^2
        // 1. memory v. CPU power: keeping an array of 100 instead of 500
        // 2. chain together: buckets for each age
        // Possible edge case: if oldest_slot never changes

        // Add the oldest page to the array of VA's that will be unmapped
        PPTE remove_pte = oldest_pfn->pte;
        va_to_unmap[num_pages_unmapped] = get_va_from_pte(remove_pte);
        num_pages_unmapped++;

        int disc_slot = find_free_disc_slot();
        if (disc_slot == -1) {
            printf("Trim Pges: out of disc\n");
            // Back out the VA
            num_pages_unmapped--;
            break;
        }

        // Calc va
        PULONG_PTR va = get_va_from_pte(remove_pte);
        // Write to disc before the batch unmap, remove_pte must still reach the frame
        memcpy((char*)page_file_base + disc_slot * PAGE_SIZE, va, PAGE_SIZE);

        // Update the pfn_metadata and pte
        set_pte_invalid(remove_pte);
        remove_pte->transition.transition = 1;
        // TODO: why do i have to reset the frame number? is it not already correct
        remove_pte->transition.frame_number = find_frame_number_from_pfn(oldest_pfn);

        oldest_pfn->disc_index = disc_slot;
        oldest_pfn->isOccupied = 3;

        // Add the pfn into the standby list, meaning
        // TODO: what is standby_ agan
        InsertTailList(&pfn_standby_list, &oldest_pfn->list);
    }

    // Batch unmap
    // TODO: it doesn't batch unmap or a little confused what > 0 would suggest
    if (num_pages_unmapped > 0) {
        if (MapUserPhysicalPagesScatter((PVOID*)va_to_unmap, num_pages_unmapped, NULL) == FALSE) {
            printf("trim_pages : scatter unmap failed\n");
        }
    }
}

//
pfn_metadata *
get_free_page() {
    // Free: If free list is not empty, take a free page
    if (IsListEmpty(&pfn_free_list) == FALSE) {
        // Move the list from the free list
        PLIST_ENTRY entry = RemoveHeadList(&pfn_free_list);
        pfn_metadata *meta = (pfn_metadata *) entry;
        return meta;
    }

    // Standby: Repurpose the page from the standby list
    if (IsListEmpty(&pfn_standby_list) == FALSE) {
        // Move the list from the standby list
        PLIST_ENTRY entry = RemoveHeadList(&pfn_standby_list);
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
        *old_pte = new_pte_contents;

        num_repurpose++;
        return meta;
    }

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

    // deadlock: when two threads want both locks, but each thread won't let go of their own
    // happens when the two threads go in diff order
    // best case: to prevent, instill one order, A -> B not B -> A as well
    // fallback: detect it soon enough, so you can "back up"

    // locks
    // directions a resource can be accessed (important for deadlock orders)
    // i.e. page table first -> then faults

    // create target functions
    // faulting and the arbitrary function
    // aging
    // trimming
    // modified page writing


    // TODO: add the modified
    // 1. write to disk
    // 2. add to standby
    // 3. repurpose

    // Trim active pages
    // TODO: figure out batching size
    // Number that represents the number of pages that will be unmapped
    int batch_size = NUMBER_OF_PHYSICAL_PAGES / 2;

    // TODO: later this will be its own thread
    // If there is no free or pages in standby, trim
    trim_pages(batch_size, physical_page_count);

    if (IsListEmpty(&pfn_standby_list) == FALSE) {
        PLIST_ENTRY entry = RemoveHeadList(&pfn_standby_list);
        pfn_metadata *meta = (pfn_metadata *) entry;

        PPTE old_pte = meta->pte;
        PTE new_pte_contents = *old_pte;
        new_pte_contents.disc.valid      = 0;
        new_pte_contents.disc.transition = 0;
        new_pte_contents.disc.disc       = 1;
        new_pte_contents.disc.disc_index = meta->disc_index;
        *old_pte = new_pte_contents;

        num_repurpose++;
        return meta;
    }

    // Get free page failed
    printf("Failed to get free page");
    DebugBreak();
    return NULL;
}

VOID
activate_page (PPTE pte, pfn_metadata *meta, ULONG_PTR pfn) {
    set_pte_valid(pte, pfn);
    pte->hardware.age = 1;
    meta->pte = pte;
    meta->isOccupied = 1;
}

BOOL
initialize_system() {
        // Return value from AllocateUserPhysicalPages
    BOOL allocated;
    // Return value from GetPrivilege()
    BOOL privilege;
    BOOL obtained_pages;
    // Can be a shared memory section handle (multi-VA) or the process handle (single-VA)
    HANDLE physical_page_handle;
    ULONG_PTR virtual_address_size;

    InitializeListHead (&pfn_free_list);
    InitializeListHead (&pfn_modified_list);
    InitializeListHead (&pfn_standby_list);

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
            printf ("Recieved no pages");
            return FALSE;
        }
    }

    // Initializing the pfn_metadata and adding it to the free_list
    setup_pfn_table(physical_page_numbers);

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
    page_file_base = create_page_file(&disc_page_count);

    // Calculate the size of our virtual memory space based on the physical pages (memory + disk) available
    virtual_address_size = (physical_page_count + disc_page_count - 1) * PAGE_SIZE;

    // Round down to a PAGE_SIZE boundary.
    virtual_address_size &= ~(PAGE_SIZE);
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

    // TODO: review these many parameters
    va_space_start = VirtualAlloc2 (NULL,
                       NULL,
                       virtual_address_size,
                       MEM_RESERVE | MEM_PHYSICAL, // MEM_PHYSICAL: reserve VA space that can be explicitly mapped
                       // to physical pages using MapUserPhysicalPages
                       PAGE_READWRITE,
                       &parameter,
                       1);

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

    return TRUE;
}

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

    // Remove the page from the standby/modified list
    RemoveEntryList (&meta->list);


    // Is it not already mapped; therefore, we only need to mark it as valid?
    if (MapUserPhysicalPages (aligned_va, 1, &pfn) == FALSE) {
        printf ("soft fault: full_virtual_memory_test : could not map VA %p to page %llX\n", aligned_va, pfn);
        DebugBreak();
        return FALSE;
    }

    // Check if page is in standby
    if (meta->isOccupied == 3) {
        // Clean our disc metadata
        empty_disc_slot(meta->disc_index);
    }

    num_soft_fault++;

    // Using the addresses of the official metadata and pfn-update it's contents
    *official_meta = meta;
    *official_pfn = pfn;
    return TRUE;
}

BOOL
handle_hard_fault(PPTE pte, PULONG_PTR aligned_va, pfn_metadata ** official_meta, ULONG_PTR * official_pfn) {
    // This a zero PTE or a disc PTE
    // Find a free physical page in memory
    pfn_metadata * meta = get_free_page();
    // Get the frame number of the free page using pointer arithmetic
    ULONG_PTR pfn = find_frame_number_from_pfn((meta));

    // Map the VA to the physical page
    if (MapUserPhysicalPages (aligned_va, 1, &pfn) == FALSE) {
        printf ("hard fault: full_virtual_memory_test : could not map VA %p to page %llX\n", aligned_va, pfn);
        DebugBreak();
        return FALSE;
    }

    // If the pte was in disc state
    if (pte->disc.disc == 1) {
        // Give the pte is in disc state, it stores the disc index
        ULONG_PTR slot_on_disc = pte->disc.disc_index;
        // Use memcpy to copy the data in the disc into the free page in memory
        memcpy(aligned_va, (char*)page_file_base + slot_on_disc * PAGE_SIZE, PAGE_SIZE);
        // Mark the data in the disc slot to be invalid
        disc_metadata[slot_on_disc] = FALSE;
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
    num_hard_fault++;

    *official_meta = meta;
    *official_pfn = pfn;
    return TRUE;
}

VOID
print_stats(unsigned i) {
    printf ("full_virtual_memory_test : finished accessing %u random virtual addresses\n", i);
    printf("Soft fault: %u \n", num_soft_fault);
    printf("Hard fault: %u \n", num_hard_fault);
    printf("Repurpose: %u \n", num_repurpose);
}

VOID
cleanup() {
    //
    // Now that we're done with our memory we can be a good
    // citizen and free it.
    //
    VirtualFree (va_space_start, 0, MEM_RELEASE);
}

VOID
// Level 3: only level that touches upon the PA, i.e., physical pages
// Program manually controls physical pages. Manage a group of physical pages, connect/disconnect to VA
full_virtual_memory_test (
    VOID
    )
{
    unsigned i;
    PULONG_PTR arbitrary_va;
    unsigned random_number;
    BOOL page_faulted;

    if (initialize_system() == FALSE) {
        printf("Initialization failed");
        DebugBreak();
        return;
    }

    for (i = 0; i < MB (1) / 100; i += 1) {
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

        // Gives a truly more random number
        random_number = rand () * rand () * rand ();

        random_number %= virtual_address_size_in_unsigned_chunks;

        //
        // Ensure the write to the arbitrary virtual address doesn't
        // straddle a PAGE_SIZE boundary just to keep things simple for
        // now.
        //

        // Clears the low 3 bits, ensure random address is 8 byte
        random_number &= ~0x7;
        // Set up mock virtual address
        arbitrary_va = va_space_start + random_number;


        page_faulted = FALSE;

        // If v=0, will lead to the except, leading to the mapping or if not using the same page that was already mapped
        __try {
            // Stamping the page which is useful for the check
            *arbitrary_va = (ULONG_PTR) arbitrary_va;
        } __except (EXCEPTION_EXECUTE_HANDLER) {
            // Flag page fault
            page_faulted = TRUE;
        }

        if (page_faulted) {
            // Get the PTE
            PPTE pte = get_pte_from_va(arbitrary_va);

            // Check if valid state
            if (pte->hardware.valid == 1) {
                continue;
            }

            pfn_metadata * meta;
            ULONG_PTR pfn;
            BOOL fault_resolution;
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
                printf("Hard fault and Soft fault failed");
                DebugBreak();
                return;
            }

            //Set pte valid and update the pfn metadata
            activate_page(pte, meta, pfn);
        }
    }

    print_stats(i);

    cleanup();
    return;
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

    full_virtual_memory_test ();

    return 0;
}

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

                        // TODO: pretend disk is an array (malloc)

                        // TODO: add a state instance variable into the struct (2 bits) for four different state values
                        // Update the PTE to reflect the disk address
                        // TODO: get rid of this

// Stores our VA to the array
// physical_page_to_virtual[i] = (ULONG_PTR) arbitrary_va;
// Second Way: i advances based on data type
// *(physical_page_to_virtual + i) = (ULONG_PTR) arbitrary_va;

//
// Unmap the virtual address translation we installed above
// now that we're done writing our value into it.
//

// Generally, we want to overwrite the oldest-created page, but given that our scenario of values is random
// The most general, and easy solution is constantly replacing the newest-created page
// In order to get the VA, we want the: VA BASE + i * 4K
// Regarding efficiency, unmapping takes a lot of energy compared to mapping and general areas
// Therefore, the goal is to unmap as match as possible in one go
// One solution: Scatter function: unmap the parameter which is an array of VAs

// Unmap the most recent created page
// Essentially: *(physical_page_to_virtual + i) is physical_page_to_virtual[i]
// Parameter type is PVOID; therefore, we cast and there is no issue given both pointers

// Xperf Cheat Sheet
// xperf -on base -stackwalk profile
// run your vmtest.exe
// xperf -top -d trace1.etl