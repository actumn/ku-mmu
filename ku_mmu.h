/* 
 * 8 bit addressing: 
 *   - address space: 256 bytes
 *   - page: 4 bytes (2 bit offset)
 *   - PDE/PTE: 1 byte
 *   2 bit: Page Directory
 *   2 bit: Page Middle Directory
 *   2 bit: Page Table
 *   2 bit: offset
 * 
 * PDE/PTE
 *   - same format
 *   - when P is 1
 *     - mapped on physical memory
 *     - 6 bits for PFN, 1 for not nused, 1 bit for present bit
 * 
 *   - when P is 0
 *     - swapped out
 *     - 7 bits for swap space offset (512 bytes, 2^7 pages)
 *     - offset starts with 1. (0 is rserved.)
 * 
 *   - example
 *     - 00000000: not mapped, not swapped out
 *     - 00000001: mapped to page frame 0 
 *     - 00001100: swapped out on swap offset 6
 */

#include <string.h>
#define PAGE_SIZE 4

///////////////////////////////////////////
/* Process control block */
typedef struct task_struct {
    char pid;
    void *pdbr;
} task_struct;

typedef struct task_struct_list_node {
    struct task_struct *data;
    struct task_struct_list_node *next;
} task_struct_list_node;
task_struct_list_node *head;

task_struct *task_struct_list_lookup(char pid) {
    task_struct_list_node *curr = head;
    while (curr != NULL) {
        if (curr->data->pid == pid) 
            return curr->data;
        
        curr = curr->next;
    }
    return NULL;
}
void task_struct_list_add(task_struct *new_task_struct) {
    if (head == NULL) {
        head = (task_struct_list_node*) malloc(sizeof(task_struct_list_node));
        head->data = new_task_struct;
        head->next = NULL;
        return;
    }
    task_struct_list_node *curr = head;
    while (curr->next != NULL) {
        curr = curr->next;
    }
    curr->next = (task_struct_list_node*) malloc(sizeof(task_struct_list_node));
    curr->next->data = new_task_struct;
    curr->next->next = NULL;
}


///////////////////////////////////////////
/* Memory space */
void *pmem_space;

// Memory Freelist
typedef struct freelist_node {
    void *addr;
    struct freelist_node *next;
} freelist_node;
freelist_node *freelist;
void freelist_init(int mem_size) {
    freelist_node *curr = (freelist_node*) malloc(sizeof(freelist_node));
    freelist = curr;
    curr->addr = NULL;
    curr->next = NULL;
    for (int i = PAGE_SIZE; i < mem_size; i += PAGE_SIZE) {
        freelist_node *new_node = (freelist_node*) malloc(sizeof(freelist_node));
        new_node->addr = pmem_space + i;
        new_node->next = NULL;
        curr->next = new_node;
        curr = new_node;
    }
}
int freelist_pop(void** addr) {
    freelist_node *curr = freelist->next;
    if (curr == NULL) {
        *addr = NULL;
        return -1;
    }
    *addr = curr->addr;
    freelist->next = curr->next;
    free(curr);
    return 0;
}

// Memory mapping list
typedef struct mappinglist_node {
    struct mappinglist_node *prev;
    char *pte;
    struct mappinglist_node *next;
} mappinglist_node;
typedef struct mapplinglist {
    struct mappinglist_node *head;
    struct mappinglist_node *tail;
} mappinglist;
mappinglist mapping;
void mapping_init() {
    mapping.head = NULL;
    mapping.tail = NULL;
}
void mapping_append(char *pte) {
    mappinglist_node *new_node = (mappinglist_node *)malloc(sizeof(mappinglist_node));
    new_node->pte = pte;

    if (mapping.head == NULL) {
        mapping.head = mapping.tail = new_node;
        mapping.head->prev = NULL;
        mapping.head->next = mapping.tail;
        mapping.tail->prev = mapping.head;
        mapping.head->next = NULL;
        return;
    }
    mapping.tail->next = new_node;
    new_node->prev = mapping.tail;
    mapping.tail = new_node;
}
int mapping_pop(char **result) {
    mappinglist_node *head = mapping.head;
    if (head == NULL) {
        return -1;
    }
    mapping.head = head->next;
    if (head->next != NULL) {
        head->next->prev = NULL;
        head->next = NULL;
    }

    *result = head->pte;
    free(head);
    return 0;
}

///////////////////////////////////////////
/* Swap space */
void *swap_space;

// swap free list
freelist_node *swap_freelist;
void swap_freelist_init(int mem_size) {
    freelist_node *curr = (freelist_node*) malloc(sizeof(freelist_node));
    swap_freelist = curr;
    curr->addr = NULL;
    curr->next = NULL;
    for (int i = PAGE_SIZE; i < mem_size; i += PAGE_SIZE) {
        freelist_node *new_node = (freelist_node*) malloc(sizeof(freelist_node));
        new_node->addr = swap_space + i;
        new_node->next = NULL;
        curr->next = new_node;
        curr = new_node;
    }
}
int swap_freelist_pop(void** addr) {
    freelist_node *curr = swap_freelist->next;
    if (curr == NULL) {
        *addr = NULL;
        return -1;
    }
    *addr = curr->addr;
    swap_freelist->next = curr->next;
    free(curr);
    return 0;
}

/////////////////////////////////////////////////////////////////////////////////////////////////////

/*
 *
 * Main Functions
 *  - ku_mmu_init: Function to initialize of user-level memory, swap space, freelist
 *  - ku_run_proc: Function to simulate context switching
 *  - ku_page_fault: Function to handle page fault 
 */


/*
 * 초기화 때 1번만 호출
 * malloc으로 
 *   physical memory나 swap sapce를 simulation하기 위한 메모리공간 할당.
 * 
 * parameter
 *   - mem_size: physical memory size (bytes)
 *     memory space와 free list 초기화
 *     (단 page frame 0은 사용하지 않는다.)
 *     memory space는 Page Directory, Page Middle Directory, Page Table 모두 함께 쓴다.
 *   - swap_size: swap disk size (bytes)
 *     (물론 simulation용도. real disk space가 아닌 memory 할당)
 * 
 * Return
 *   - Pointer: 할당할 메모리 시작 주소
 *   - 0: fail
 */
void *ku_mmu_init(unsigned int mem_size, unsigned int swap_size) {
    /* Init pmem_space */
    pmem_space = malloc(mem_size);
    memset(pmem_space, 0, mem_size);
    if (pmem_space == NULL) {
        return 0;
    }

    /* Init swap_space */
    swap_space = malloc(swap_size);
    memset(swap_space, 0, swap_size);
    if (swap_space == NULL) {
        return 0;
    }
    
    /* Init freelist */
    freelist_init(mem_size);
    swap_freelist_init(swap_size);
    mapping_init();

    return pmem_space;
}

/*
 * 해당 pid로 context switch
 * (실제로 fork, signal, 실행 등은 하지 않음. 그냥 simulation)
 * PCB control은 필요하다.
 * pid가 새로운 거라면 새로운 실행 => PCB 만들고 관리. page directory로 만들자.
 * 
 * parameter
 *   - pid
 *   - ku_cfs: PDBR의 역할.
 *     8 bit PDE를 가리킨다.
 *     해당 pid의 page directory base address를 갖도록.
 * 
 * Return
 *   - 0: Success
 *   - -1: fail
 */
int ku_run_proc(char pid, void **ku_cr3) {
    task_struct *proc = task_struct_list_lookup(pid);
    if (proc == NULL) {
        void *addr = NULL;
        if (freelist_pop(&addr) != 0) {
            void *swap_page = NULL;
            if (swap_freelist_pop(&swap_page) != 0) {
                // No more swap space 
                // Error
                return -1;
            }
            
            char *mapped_pte_addr; 
            if (mapping_pop(&mapped_pte_addr) != 0) {
                // No more page to swap out
                // Error
                return -1;
            };
            char mapped_pte = *mapped_pte_addr;
            int mapped_page_pfn = (mapped_pte & 0xFC);
            void *page_addr = pmem_space + mapped_page_pfn;

            // memory page -> swap page
            memcpy(swap_page, page_addr, PAGE_SIZE);
            // set 0 on page
            memset(page_addr, 0, PAGE_SIZE);

            // swapped pte: 00000010
            mapped_pte = ((swap_page - swap_space) >> 1) & 0xFE;
            *mapped_pte_addr = mapped_pte; 
            
            addr = page_addr;
        }
        task_struct* new_proc = (task_struct*) malloc(sizeof(task_struct));
        new_proc->pid = pid;
        new_proc->pdbr = addr;
        task_struct_list_add(new_proc);
        proc = new_proc;
    }
    *ku_cr3 = proc->pdbr;
    return 0;
}

/*
 * application이 접근하려는 page가 present가 되어야 한다.
 * (page fault 발생 요인은 Page Middle Directory가 없다던지, Page Table이 없다던지, swap out 상태라던지)
 *   - demand paging => Page Table Update / Swapping => Swap in
 *   - 물리 메모리 할당 필요. 물리메모리가 할당 불가능한 경우 eviction이 필요
 *   - 이 때 eviction 알고리즘은 FIFO
 *   - Page directories, Page middle directories, Page tables는 swap out 되지 않는다
 *   - swap 공간 관리 free list ( swap 공간 어느 페이지가 사용되는지, 사용되지 않는지 )
 * 
 * Parameter
 *   - pid: page fault pid
 *   - va: page fault가 발생한 virtual address
 * 
 * Return
 *   - 0: success
 *   - -1: fail
 */
int ku_page_fault(char pid, char va) {
    task_struct *proc = task_struct_list_lookup(pid);
    if (proc == NULL)
        return -1;
    void *pdbr = proc->pdbr;
    if (pdbr == NULL)
        return -1;


    // page directory
    int pd_idx = (va & 0xC0) >> 6;
    void *PDEAddr = pdbr + pd_idx;
    char pde = *(char*)PDEAddr; 
    if (pde == 0x00) {
        // Page fault: Not allocated page middle directory.
        // Allocate new page middle directory 
        void* new_pmd_addr = NULL;
        if (freelist_pop(&new_pmd_addr) != 0) {
            // If no more space on memory
            // Swap out existing page.
            void *swap_page = NULL;
            if (swap_freelist_pop(&swap_page) != 0) {
                // No more swap space 
                // Error
                return -1;
            }
            
            char *mapped_pte_addr;
            if (mapping_pop(&mapped_pte_addr) != 0) {
                // No more page to swap out
                // Error
                return -1;
            };
            char mapped_pte = *mapped_pte_addr;
            int mapped_page_pfn = (mapped_pte & 0xFC);
            void *page_addr = pmem_space + mapped_page_pfn;

            // memory page -> swap page
            memcpy(swap_page, page_addr, PAGE_SIZE);
            // set 0 on page
            memset(page_addr, 0, PAGE_SIZE);

            // swapped pte: 00000010
            mapped_pte = ((swap_page - swap_space) >> 1) & 0xFE;
            *mapped_pte_addr = mapped_pte; 
            
            new_pmd_addr = page_addr;
        }

        // pde: 00000101
        pde = (new_pmd_addr - pmem_space) | 1;
        *(char*)PDEAddr = pde;
    }
    int pde_pfn = (pde & 0xFC);
    
    // page middle directory
    void *pmdbr = pmem_space + pde_pfn;
    int pmd_idx = (va & 0x30) >> 4;
    void *PMDEAddr = pmdbr + pmd_idx;
    char pmde = *(char*)PMDEAddr;
    if (pmde == 0x00) {
        // Page fault: Not allocated page table.
        // Allocate new page table
        void* new_pt_addr = NULL;
        if (freelist_pop(&new_pt_addr) != 0) {
            // If no more space on memory
            // Swap out existing page.
            void *swap_page = NULL;
            if (swap_freelist_pop(&swap_page) != 0) {
                // No more swap space 
                // Error
                return -1;
            }

            char *mapped_pte_addr; 
            if (mapping_pop(&mapped_pte_addr) != 0) {
                // No more page to swap out
                // Error
                return -1;
            };
            char mapped_pte = *mapped_pte_addr;
            int mapped_page_pfn = (mapped_pte & 0xFC);
            void *page_addr = pmem_space + mapped_page_pfn;

            // memory page -> swap page
            memcpy(swap_page, page_addr, PAGE_SIZE);
            // set 0 on page
            memset(page_addr, 0, PAGE_SIZE);

            // swapped pte: 00000010
            mapped_pte = ((swap_page - swap_space) >> 1) & 0xFE;
            *mapped_pte_addr = mapped_pte; 
            
            new_pt_addr = page_addr;
        }

        // pmde: 00001001
        pmde = (new_pt_addr - pmem_space) | 1;
        *(char*)PMDEAddr = pmde;
    }
    int pmde_pfn = (pmde & 0xFC);

    // page table
    void *ptbr = pmem_space + pmde_pfn;
    int pt_idx = (va & 0x0C) >> 2;
    void *PTEAddr = ptbr + pt_idx;
    char pte = *(char*)PTEAddr;
    if (pte == 0x00) {
        // Page fault: Not allocated page.
        // Allocate new page
        void* new_page_addr = NULL;
        if (freelist_pop(&new_page_addr) != 0) {
            // If no more space on memory
            // Swap out existing page.
            void *swap_page = NULL;
            if (swap_freelist_pop(&swap_page) != 0) {
                // No more swap space 
                // Error
                return -1;
            }
            
            char *mapped_pte_addr; 
            if (mapping_pop(&mapped_pte_addr) != 0) {
                // No more page to swap out
                // Error
                return -1;
            };
            char mapped_pte = *mapped_pte_addr;
            int mapped_page_pfn = (mapped_pte & 0xFC);
            void *page_addr = pmem_space + mapped_page_pfn;

            // memory page -> swap page
            memcpy(swap_page, page_addr, PAGE_SIZE);
            // set 0 on page
            memset(page_addr, 0, PAGE_SIZE);

            // swapped pte: 00000010
            mapped_pte = ((swap_page - swap_space) >> 1) & 0xFE;
            *mapped_pte_addr = mapped_pte; 
            
            new_page_addr = page_addr;
        };
        // pte: 00001101
        pte = (new_page_addr - pmem_space) | 1;
        *(char*)PTEAddr = pte;

        // finally append PTEAddr to mapping list
        mapping_append((char*)PTEAddr);

        return 0;
    }
    if (pte == 0x01) {
        // exception case. nothing to do.
        return -1;
    }
    if ((pte & 0x01) == 0) {
        // Page fault: data in swap space
        char *mapped_pte_addr; 
        if (mapping_pop(&mapped_pte_addr) != 0) {
            // No more page to swap out
            // Error
            return -1;
        };
        char mapped_pte = *mapped_pte_addr;
        int mapped_page_pfn = (mapped_pte & 0xFC);
        void *mapped_page_addr = pmem_space + mapped_page_pfn;

        int pte_pfn = ((pte & 0xFE) << 1);
        void *pte_page_addr = swap_space + pte_pfn;

        
        // page memory swap
        char temp_page_buffer[PAGE_SIZE];
        memcpy(temp_page_buffer, mapped_page_addr, PAGE_SIZE);
        memcpy(mapped_page_addr, pte_page_addr, PAGE_SIZE);
        memcpy(pte_page_addr, temp_page_buffer, PAGE_SIZE);
        
        *mapped_pte_addr = (pte_pfn >> 1) & 0xFE;
        *(char*)PTEAddr = mapped_page_pfn | 1;    

        mapping_append((char*)PTEAddr);
        return 0;
    }
    
    // Not executed here.  
    return -1;
}
