/*
 * osFree NUMA-Aware Memory Allocator
 * Copyright (c) 2024 osFree Project
 *
 * NUMA topology support for optimal memory placement
 */

#include <os3/memory.h>
#include <os3/numa.h>
#include <os3/acpi.h>
#include <os3/smp.h>
#include <os3/spinlock.h>
#include <os3/debug.h>

/* Maximum NUMA nodes */
#define MAX_NUMA_NODES  64

/* NUMA node memory information */
typedef struct numa_mem_info {
    uint64_t start_pfn;         /* Start page frame number */
    uint64_t end_pfn;           /* End page frame number */
    uint64_t free_pages;        /* Free pages count */
    uint64_t total_pages;       /* Total pages */
    
    /* Free page lists per order (buddy allocator) */
    struct list_head free_list[MAX_ORDER];
    uint32_t free_count[MAX_ORDER];
    
    spinlock_t lock;
    
} numa_mem_info_t;

/* NUMA topology */
typedef struct numa_topology {
    uint32_t num_nodes;
    uint32_t num_cpus;
    
    /* Node information */
    numa_mem_info_t nodes[MAX_NUMA_NODES];
    
    /* CPU to node mapping */
    uint8_t cpu_to_node[MAX_CPUS];
    
    /* Node distance matrix */
    uint8_t distance[MAX_NUMA_NODES][MAX_NUMA_NODES];
    
    /* Fallback order for each node (sorted by distance) */
    uint8_t fallback[MAX_NUMA_NODES][MAX_NUMA_NODES];
    
} numa_topology_t;

static numa_topology_t numa_topo;
static int numa_enabled = 0;

/*
 * Initialize NUMA topology from ACPI
 */
int numa_init(void)
{
    int i, j;
    
    kprintf("NUMA: Initializing NUMA topology\n");
    
    memset(&numa_topo, 0, sizeof(numa_topo));
    
    /* Parse SRAT for CPU and memory affinity */
    if (acpi_parse_numa() < 0) {
        kprintf("NUMA: No SRAT found, assuming UMA system\n");
        numa_topo.num_nodes = 1;
        numa_enabled = 0;
        return 0;
    }
    
    numa_topo.num_nodes = acpi_info.numa_nodes;
    
    if (numa_topo.num_nodes <= 1) {
        kprintf("NUMA: Single node system, NUMA disabled\n");
        numa_enabled = 0;
        return 0;
    }
    
    /* Initialize per-node structures */
    for (i = 0; i < numa_topo.num_nodes; i++) {
        spin_lock_init(&numa_topo.nodes[i].lock);
        
        for (j = 0; j < MAX_ORDER; j++) {
            INIT_LIST_HEAD(&numa_topo.nodes[i].free_list[j]);
            numa_topo.nodes[i].free_count[j] = 0;
        }
    }
    
    /* Build CPU to node mapping */
    for (i = 0; i < smp_info.cpu_possible; i++) {
        numa_topo.cpu_to_node[i] = acpi_get_numa_node(
            acpi_info.cpus[i].apic_id);
    }
    
    /* Parse SLIT for node distances */
    if (acpi_info.slit) {
        for (i = 0; i < numa_topo.num_nodes; i++) {
            for (j = 0; j < numa_topo.num_nodes; j++) {
                numa_topo.distance[i][j] = acpi_get_numa_distance(i, j);
            }
        }
    } else {
        /* Default distances: local = 10, remote = 20 */
        for (i = 0; i < numa_topo.num_nodes; i++) {
            for (j = 0; j < numa_topo.num_nodes; j++) {
                numa_topo.distance[i][j] = (i == j) ? 10 : 20;
            }
        }
    }
    
    /* Build fallback order for each node */
    numa_build_fallback_order();
    
    numa_enabled = 1;
    
    kprintf("NUMA: %d nodes detected\n", numa_topo.num_nodes);
    
    /* Print distance matrix */
    kprintf("NUMA: Distance matrix:\n");
    for (i = 0; i < numa_topo.num_nodes; i++) {
        kprintf("  Node %d: ", i);
        for (j = 0; j < numa_topo.num_nodes; j++) {
            kprintf("%3d ", numa_topo.distance[i][j]);
        }
        kprintf("\n");
    }
    
    return 0;
}

/*
 * Build fallback order based on distances
 */
static void numa_build_fallback_order(void)
{
    int i, j, k;
    
    for (i = 0; i < numa_topo.num_nodes; i++) {
        /* Copy node indices */
        for (j = 0; j < numa_topo.num_nodes; j++) {
            numa_topo.fallback[i][j] = j;
        }
        
        /* Sort by distance from node i (simple bubble sort) */
        for (j = 0; j < numa_topo.num_nodes - 1; j++) {
            for (k = 0; k < numa_topo.num_nodes - j - 1; k++) {
                uint8_t n1 = numa_topo.fallback[i][k];
                uint8_t n2 = numa_topo.fallback[i][k + 1];
                
                if (numa_topo.distance[i][n1] > numa_topo.distance[i][n2]) {
                    numa_topo.fallback[i][k] = n2;
                    numa_topo.fallback[i][k + 1] = n1;
                }
            }
        }
    }
}

/*
 * Add memory region to NUMA node
 */
void numa_add_memory(uint32_t node, uint64_t start, uint64_t size)
{
    numa_mem_info_t *nmi;
    
    if (node >= MAX_NUMA_NODES || node >= numa_topo.num_nodes) {
        kprintf("NUMA: Invalid node %d\n", node);
        return;
    }
    
    nmi = &numa_topo.nodes[node];
    
    uint64_t start_pfn = start >> PAGE_SHIFT;
    uint64_t end_pfn = (start + size) >> PAGE_SHIFT;
    
    if (nmi->total_pages == 0) {
        nmi->start_pfn = start_pfn;
        nmi->end_pfn = end_pfn;
    } else {
        if (start_pfn < nmi->start_pfn)
            nmi->start_pfn = start_pfn;
        if (end_pfn > nmi->end_pfn)
            nmi->end_pfn = end_pfn;
    }
    
    nmi->total_pages += (end_pfn - start_pfn);
    
    kprintf("NUMA: Node %d: added %llu MB at 0x%llx\n",
            node, size / (1024 * 1024), start);
}

/*
 * Allocate pages from specific NUMA node
 */
void *numa_alloc_pages(uint32_t node, uint32_t order)
{
    numa_mem_info_t *nmi;
    struct page *page;
    irqflags_t flags;
    int i;
    
    if (!numa_enabled) {
        /* Fall back to regular allocator */
        return alloc_pages(order);
    }
    
    /* Try requested node first */
    nmi = &numa_topo.nodes[node];
    
    spin_lock_irqsave(&nmi->lock, &flags);
    
    if (nmi->free_count[order] > 0) {
        page = list_first_entry(&nmi->free_list[order], 
                                struct page, list);
        list_del(&page->list);
        nmi->free_count[order]--;
        nmi->free_pages -= (1 << order);
        
        spin_unlock_irqrestore(&nmi->lock, flags);
        return page_to_virt(page);
    }
    
    spin_unlock_irqrestore(&nmi->lock, flags);
    
    /* Try higher orders on same node (splitting) */
    for (i = order + 1; i < MAX_ORDER; i++) {
        spin_lock_irqsave(&nmi->lock, &flags);
        
        if (nmi->free_count[i] > 0) {
            page = list_first_entry(&nmi->free_list[i], 
                                    struct page, list);
            list_del(&page->list);
            nmi->free_count[i]--;
            
            /* Split the block */
            while (i > order) {
                i--;
                struct page *buddy = page + (1 << i);
                list_add(&buddy->list, &nmi->free_list[i]);
                nmi->free_count[i]++;
            }
            
            nmi->free_pages -= (1 << order);
            spin_unlock_irqrestore(&nmi->lock, flags);
            return page_to_virt(page);
        }
        
        spin_unlock_irqrestore(&nmi->lock, flags);
    }
    
    /* Try fallback nodes in distance order */
    for (i = 1; i < numa_topo.num_nodes; i++) {
        uint32_t fallback_node = numa_topo.fallback[node][i];
        void *ptr = numa_alloc_pages_strict(fallback_node, order);
        if (ptr) {
            return ptr;
        }
    }
    
    return NULL;
}

/*
 * Allocate from specific node only (no fallback)
 */
void *numa_alloc_pages_strict(uint32_t node, uint32_t order)
{
    numa_mem_info_t *nmi;
    struct page *page;
    irqflags_t flags;
    
    if (node >= numa_topo.num_nodes) {
        return NULL;
    }
    
    nmi = &numa_topo.nodes[node];
    
    spin_lock_irqsave(&nmi->lock, &flags);
    
    if (nmi->free_count[order] > 0) {
        page = list_first_entry(&nmi->free_list[order], 
                                struct page, list);
        list_del(&page->list);
        nmi->free_count[order]--;
        nmi->free_pages -= (1 << order);
        
        spin_unlock_irqrestore(&nmi->lock, flags);
        return page_to_virt(page);
    }
    
    spin_unlock_irqrestore(&nmi->lock, flags);
    return NULL;
}

/*
 * Free pages back to their NUMA node
 */
void numa_free_pages(void *ptr, uint32_t order)
{
    struct page *page = virt_to_page(ptr);
    uint32_t node = page->numa_node;
    numa_mem_info_t *nmi = &numa_topo.nodes[node];
    irqflags_t flags;
    
    spin_lock_irqsave(&nmi->lock, &flags);
    
    /* Try to merge with buddy */
    while (order < MAX_ORDER - 1) {
        uint64_t pfn = page_to_pfn(page);
        uint64_t buddy_pfn = pfn ^ (1 << order);
        struct page *buddy = pfn_to_page(buddy_pfn);
        
        /* Check if buddy is free and same order */
        if (buddy->flags & PAGE_FLAG_BUDDY &&
            buddy->order == order) {
            /* Remove buddy from free list */
            list_del(&buddy->list);
            nmi->free_count[order]--;
            buddy->flags &= ~PAGE_FLAG_BUDDY;
            
            /* Merge: use lower address as new block */
            if (buddy_pfn < pfn) {
                page = buddy;
            }
            order++;
        } else {
            break;
        }
    }
    
    /* Add merged block to free list */
    page->order = order;
    page->flags |= PAGE_FLAG_BUDDY;
    list_add(&page->list, &nmi->free_list[order]);
    nmi->free_count[order]++;
    nmi->free_pages += (1 << order);
    
    spin_unlock_irqrestore(&nmi->lock, flags);
}

/*
 * Kernel allocation wrapper - NUMA aware
 */
void *kmalloc_node(size_t size, uint32_t node)
{
    uint32_t order = 0;
    size_t alloc_size = PAGE_SIZE;
    
    /* Round up to power of 2 pages */
    while (alloc_size < size) {
        alloc_size <<= 1;
        order++;
    }
    
    return numa_alloc_pages(node, order);
}

/*
 * Get NUMA node for current CPU
 */
uint32_t numa_node_id(void)
{
    if (!numa_enabled) {
        return 0;
    }
    return numa_topo.cpu_to_node[smp_processor_id()];
}

/*
 * Get NUMA node for a given CPU
 */
uint32_t cpu_to_node(uint32_t cpu)
{
    if (!numa_enabled || cpu >= MAX_CPUS) {
        return 0;
    }
    return numa_topo.cpu_to_node[cpu];
}

/*
 * Get distance between two NUMA nodes
 */
uint8_t numa_distance(uint32_t node1, uint32_t node2)
{
    if (!numa_enabled) {
        return 10;  /* Same node */
    }
    if (node1 >= numa_topo.num_nodes || node2 >= numa_topo.num_nodes) {
        return 255; /* Invalid */
    }
    return numa_topo.distance[node1][node2];
}

/*
 * Get number of NUMA nodes
 */
uint32_t numa_num_nodes(void)
{
    return numa_enabled ? numa_topo.num_nodes : 1;
}

/*
 * Check if NUMA is enabled
 */
int numa_is_enabled(void)
{
    return numa_enabled;
}

/*
 * Print NUMA statistics
 */
void numa_print_stats(void)
{
    int i;
    
    kprintf("NUMA Statistics:\n");
    kprintf("  Nodes: %d\n", numa_topo.num_nodes);
    kprintf("  NUMA enabled: %s\n", numa_enabled ? "yes" : "no");
    
    for (i = 0; i < numa_topo.num_nodes; i++) {
        numa_mem_info_t *nmi = &numa_topo.nodes[i];
        kprintf("  Node %d: %llu/%llu pages free (%llu MB total)\n",
                i, nmi->free_pages, nmi->total_pages,
                (nmi->total_pages * PAGE_SIZE) / (1024 * 1024));
    }
}
