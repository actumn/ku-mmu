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
#include <assert.h>
#define PAGE_SIZE 4

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



/* Memory */
void *pmem_space, *swap_space;

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
    for (int i = 0; i < mem_size; i += PAGE_SIZE) {
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
int freelist_debug() {
    printf("mem_space: %p\n", pmem_space);
    freelist_node *curr = freelist;
    int i = 0;
    while (curr != NULL) {
        printf("free_list[%d]: %p\n", i, curr->addr);
        curr = curr->next;
        i += 1;
    }
}

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
            // TODO:: swap out
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
    // printf("PDEAddr: %p, value: %x\n", PDEAddr, *(char*)PDEAddr);
    if (pde == 0x00) {
        void* new_pdm_addr = NULL;
        if (freelist_pop(&new_pdm_addr) != 0) {
            // TODO: swap out
        }
        assert(((new_pdm_addr - pmem_space) & 0x03) == 0);

        // pde: 00000101
        pde = (new_pdm_addr - pmem_space) | 1;
        *(char*)PDEAddr = pde;
        // printf("PDEAddr: %p, value: %x\n", PDEAddr, *(char*)PDEAddr);
        // printf("%x\n", pde);
    }
    int pde_pfn = (pde & 0xFC);
    
    // page middle directory
    void *pmdbr = pmem_space + pde_pfn;
    int pmd_idx = (va & 0x30) >> 4;
    void *PMDEAddr = pmdbr + pmd_idx;
    char pmde = *(char*)PMDEAddr;
    // printf("PMDEAddr: %p, value: %x\n", PMDEAddr, *(char*)PMDEAddr);
    if (pmde == 0x00) {
        void* new_pt_addr = NULL;
        if (freelist_pop(&new_pt_addr) != 0) {
            // TODO: swap out
        }
        assert(((new_pt_addr - pmem_space) & 0x03) == 0);

        // pmde: 00001001
        pmde = (new_pt_addr - pmem_space) | 1;
        *(char*)PMDEAddr = pmde;
        // printf("PDEAddr: %p, value: %x\n", PMDEAddr, *(char*)PMDEAddr);
        // printf("%x\n", pmde);
    }
    int pmde_pfn = (pmde & 0xFC);

    // page table
    void *ptbr = pmem_space + pmde_pfn;
    int pt_idx = (va & 0x0C) >> 2;
    void *PTAddr = ptbr + pt_idx;
    char pte = *(char*)PTAddr;
    // printf("PTAddr: %p, value: %x\n", PTAddr, *(char*)PTAddr);
    if (pte == 0x00) {
        void* new_pt_addr = NULL;
        if (freelist_pop(&new_pt_addr) != 0) {
            // TODO: swap out
        };
        assert(((new_pt_addr - pmem_space) & 0x03) == 0);

        // pte: 00001101
        pte = (new_pt_addr - pmem_space) | 1;
        *(char*)PTAddr = pte;
        // printf("PTAddr: %p, value: %x\n", PTAddr, *(char*)PTAddr);
        // printf("%x\n", pte);
    }
    if (pte == 0x01) {
        // exception case. nothing to do.
        return -1;
    }
    if ((pte & 0x01) == 0) {
        // TODO:: swap in 구현
    }
    // int pt_pfn = (pte & 0xFC);

    // void *page = pmem_space + pt_pfn;
    // int offset = (va & 0x03);
    // char *physical_memory = (char*)(page + offset);
    // printf("%d\n", (int) *physical_memory);
    
    return 0;
}
