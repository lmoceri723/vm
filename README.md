# VM
## A fully working virtual memory management state machine
### This program uses multi-threading to more effectively manage memory. It creates and manages virtual address space, holding a physical page pool and allocating a paging file much larger than its page pool in order to illustrate the effectiveness of virtual memory. It controls the entire process of memory management: managing free, standby, and modified PFN lists; handling page faults; aging and trimming active pages; writing modified pages to the paging file; repurposing standby pages; and retrieving and remapping pages from the paging file into physical memory.

#### This program accesses 104857 random virtual addresses in 1156 ms (1.156000 s) on a computer with 16GB of 3200 MHz DDR4 RAM and a Ryzen 9 5900HX with 16 cores. Nearly all of these accesses involve resolving page faults.
#### Its performance is slower than the single-threaded version, as multi-threaing is mostly, but not entirely implemented in this program. Additionally, it uses a less sophisticated aging model that had to be implemented for compatibility with multi-threading. Expect these two issues to be resolved in the near future.

For a surface-level explanation of the memory manager, click here: https://docs.google.com/document/d/1vOB8vep5XL9FHHsuzr9Utfq603oT_31MSur2wbtVquo/edit?usp=sharing
