# VM
## A fully working virtual memory management state machine
### This program uses multi-threading to more effectively manage memory. It creates and manages virtual address space, holding a physical page pool and allocating a paging file much larger than its page pool in order to illustrate the effectiveness of virtual memory. It controls the entire process of memory management: managing free, standby, and modified PFN lists; handling page faults; aging and trimming active pages; writing modified pages to the paging file; repurposing standby pages; and retrieving and remapping pages from the paging file into physical memory.

For a surface-level explanation of the memory manager, click here: https://docs.google.com/document/d/1vOB8vep5XL9FHHsuzr9Utfq603oT_31MSur2wbtVquo/edit?usp=sharing
