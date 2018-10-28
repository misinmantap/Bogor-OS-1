DEFINE_SPINLOCK(kmap_gen_lock);

/*  checkpatch says don't init this to 0.  */
unsigned long long kmap_generation;
void __init mem_init(void)
{
	/*  No idea where this is actually declared.  Seems to evade LXR.  */
	free_all_bootmem();
	mem_init_print_info(NULL);

	/*
	 *  To-Do:  someone somewhere should wipe out the bootmem map
	 *  after we're done?
	 */

	/*
	 * This can be moved to some more virtual-memory-specific
	 * initialization hook at some point.  Set the init_mm
	 * descriptors "context" value to point to the initial
	 * kernel segment table's physical address.
	 */
	init_mm.context.ptbase = __pa(init_mm.pgd);
}

/*
 * free_initmem - frees memory used by stuff declared with __init
 *
 * Todo:  free pages between __init_begin and __init_end; possibly
 * some devtree related stuff as well.
 */
void __ref free_initmem(void)
{
}

/*
 * free_initrd_mem - frees...  initrd memory.
 * @start - start of init memory
 * @end - end of init memory
 *
 * Apparently has to be passed the address of the initrd memory.
 *
 * Wrapped by #ifdef CONFIG_BLKDEV_INITRD
 */
void free_initrd_mem(unsigned long start, unsigned long end)
{
}

void sync_icache_dcache(pte_t pte)
{
	unsigned long addr;
	struct page *page;

	page = pte_page(pte);
	addr = (unsigned long) page_address(page);

	__vmcache_idsync(addr, PAGE_SIZE);
}

/*
 * In order to set up page allocator "nodes",
 * somebody has to call free_area_init() for UMA.
 *
 * In this mode, we only have one pg_data_t
 * structure: contig_mem_data.
 */
void __init paging_init(void)
{
	unsigned long zones_sizes[MAX_NR_ZONES] = {0, };

	/*
	 *  This is not particularly well documented anywhere, but
	 *  give ZONE_NORMAL all the memory, including the big holes
	 *  left by the kernel+bootmem_map which are already left as reserved
	 *  in the bootmem_map; free_area_init should see those bits and
	 *  adjust accordingly.
	 */

	zones_sizes[ZONE_NORMAL] = max_low_pfn;

	free_area_init(zones_sizes);  /*  sets up the zonelists and mem_map  */

	/*
	 * Start of high memory area.  Will probably need something more
	 * fancy if we...  get more fancy.
	 */
	high_memory = (void *)((bootmem_lastpg + 1) << PAGE_SHIFT);
}

#ifndef DMA_RESERVE
#define DMA_RESERVE		(4)
#endif

#define DMA_CHUNKSIZE		(1<<22)
#define DMA_RESERVED_BYTES	(DMA_RESERVE * DMA_CHUNKSIZE)

/*
 * Pick out the memory size.  We look for mem=size,
 * where size is "size[KkMm]"
 */
static int __init early_mem(char *p)
{
	unsigned long size;
	char *endp;

	size = memparse(p, &endp);

	bootmem_lastpg = PFN_DOWN(size);

	return 0;
}
early_param("mem", early_mem);

size_t hexagon_coherent_pool_size = (size_t) (DMA_RESERVE << 22);

void __init setup_arch_memory(void)
{
	/*  XXX Todo: this probably should be cleaned up  */
	u32 *segtable = (u32 *) &swapper_pg_dir[0];
	u32 *segtable_end;

	/*
	 * Set up boot memory allocator
	 *
	 * The Gorman book also talks about these functions.
	 * This needs to change for highmem setups.
	 */

	/*  Prior to this, bootmem_lastpg is actually mem size  */
	bootmem_lastpg += ARCH_PFN_OFFSET;

	/* Memory size needs to be a multiple of 16M */
	bootmem_lastpg = PFN_DOWN((bootmem_lastpg << PAGE_SHIFT) &
		~((BIG_KERNEL_PAGE_SIZE) - 1));

	memblock_add(PHYS_OFFSET,
		     (bootmem_lastpg - ARCH_PFN_OFFSET) << PAGE_SHIFT);

	/* Reserve kernel text/data/bss */
	memblock_reserve(PHYS_OFFSET,
			 (bootmem_startpg - ARCH_PFN_OFFSET) << PAGE_SHIFT);
	/*
	 * Reserve the top DMA_RESERVE bytes of RAM for DMA (uncached)
	 * memory allocation
	 */
	max_low_pfn = bootmem_lastpg - PFN_DOWN(DMA_RESERVED_BYTES);
	min_low_pfn = ARCH_PFN_OFFSET;
	memblock_reserve(PFN_PHYS(max_low_pfn), DMA_RESERVED_BYTES);

	printk(KERN_INFO "bootmem_startpg:  0x%08lx\n", bootmem_startpg);
	printk(KERN_INFO "bootmem_lastpg:  0x%08lx\n", bootmem_lastpg);
	printk(KERN_INFO "min_low_pfn:  0x%08lx\n", min_low_pfn);
	printk(KERN_INFO "max_low_pfn:  0x%08lx\n", max_low_pfn);

	/*
	 * The default VM page tables (will be) populated with
	 * VA=PA+PAGE_OFFSET mapping.  We go in and invalidate entries
	 * higher than what we have memory for.
	 */

	/*  this is pointer arithmetic; each entry covers 4MB  */
	segtable = segtable + (PAGE_OFFSET >> 22);

	/*  this actually only goes to the end of the first gig  */
	segtable_end = segtable + (1<<(30-22));

	/*
	 * Move forward to the start of empty pages; take into account
	 * phys_offset shift.
	 */

	segtable += (bootmem_lastpg-ARCH_PFN_OFFSET)>>(22-PAGE_SHIFT);
	{
		int i;

		for (i = 1 ; i <= DMA_RESERVE ; i++)
			segtable[-i] = ((segtable[-i] & __HVM_PTE_PGMASK_4MB)
				| __HVM_PTE_R | __HVM_PTE_W | __HVM_PTE_X
				| __HEXAGON_C_UNC << 6
				| __HVM_PDE_S_4MB);
	}

	printk(KERN_INFO "clearing segtable from %p to %p\n", segtable,
		segtable_end);
	while (segtable < (segtable_end-8))
		*(segtable++) = __HVM_PDE_S_INVALID;
	/* stop the pointer at the device I/O 4MB page  */

	printk(KERN_INFO "segtable = %p (should be equal to _K_io_map)\n",
		segtable);

#if 0
	/*  Other half of the early device table from vm_init_segtable. */
	printk(KERN_INFO "&_K_init_devicetable = 0x%08x\n",
		(unsigned long) _K_init_devicetable-PAGE_OFFSET);
	*segtable = ((u32) (unsigned long) _K_init_devicetable-PAGE_OFFSET) |
		__HVM_PDE_S_4KB;
	printk(KERN_INFO "*segtable = 0x%08x\n", *segtable);
#endif

	/*
	 *  The bootmem allocator seemingly just lives to feed memory
	 *  to the paging system
	 */
	printk(KERN_INFO "PAGE_SIZE=%lu\n", PAGE_SIZE);
	paging_init();  /*  See Gorman Book, 2.3  */
}
