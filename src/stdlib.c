//Neutron Project
//C Standard Library

#include <efi.h>
#include <efilib.h>

#include "./stdlib.h"
#include "./drivers/gfx.h"
#include "./mtask/mtask.h"

EFI_SYSTEM_TABLE* krnl_get_efi_systable(void);

free_block_t* first_free_block;
void* gen_free_base;
void* gen_free_top;
uint64_t bad_ram_size = 0;
uint64_t total_ram_size = 0;
uint64_t used_ram_size = 0;

/*
 * Returns the amount of RAM usable by Neutron
 */
uint64_t stdlib_usable_ram(void){
    return total_ram_size;
}

/*
 * Returns the amount of RAM currently being used by Neutron
 */
uint64_t stdlib_used_ram(void){
    return used_ram_size;
}

/*
 * Abort execution
 */
void abort(){
    //Stop the scheduler
    mtask_stop();
    //Hang
    while(1);
}

/*
 * Initialize the dynamic memory allocator
 */
uint64_t dram_init(void){
    //Get the memory map from EFI
    EFI_MEMORY_DESCRIPTOR* buf;
    uint64_t desc_size;
    uint32_t desc_ver;
    uint64_t size, map_key, mapping_size;
    EFI_MEMORY_DESCRIPTOR* desc;
    EFI_STATUS status;
    uint32_t i = 0;
    //Allocate some memory
    size = sizeof(EFI_MEMORY_DESCRIPTOR) * 31;
    mem_map_retry:
    size += sizeof(EFI_MEMORY_DESCRIPTOR) * 31;
    status = krnl_get_efi_systable()->BootServices->AllocatePool(EfiLoaderData, size, (void*)&buf);
    if(EFI_ERROR(status)){
        krnl_get_efi_systable()->ConOut->OutputString(krnl_get_efi_systable()->ConOut,
            (CHAR16*)L"Failed to allocate memory for the memory map\r\n");
        while(1);
    }
    //Map the memory
    status = krnl_get_efi_systable()->BootServices->GetMemoryMap(&size, buf, &map_key, &desc_size, &desc_ver);
    //Re-allocate the buffer with a different size if the current one isn't sufficient
    if(EFI_ERROR(status)){
        if(status == EFI_BUFFER_TOO_SMALL){
            krnl_get_efi_systable()->BootServices->FreePool(buf);
            goto mem_map_retry;
        } else {
            krnl_get_efi_systable()->ConOut->OutputString(krnl_get_efi_systable()->ConOut,
                (CHAR16*)L"Failed to get the memory map\r\n");
            while(1);
        }
    }

    desc = buf;
    void* best_block_start = NULL;
    uint64_t best_block_size = 0;
    //Fetch the next descriptor
    while((uint8_t*)desc < (uint8_t*)buf + size){
        mapping_size = desc->NumberOfPages * EFI_PAGE_SIZE;

        //If a new free memory block was found, record it
        if(desc->Type == EfiConventionalMemory && mapping_size > best_block_size){
            best_block_size = mapping_size;
            best_block_start = (void*)desc->PhysicalStart;
        }
        //Record bad RAM
        else if(desc->Type == EfiUnusableMemory){
            bad_ram_size += mapping_size;
        }

        desc = (void*)desc + desc_size;
        i++;
    }

    //Set up general free heap
    gen_free_base = best_block_start;
    gen_free_top = best_block_start + best_block_size;
    total_ram_size += best_block_size;

    if(gen_free_top == NULL){
        krnl_get_efi_systable()->ConOut->OutputString(krnl_get_efi_systable()->ConOut,
            (CHAR16*)L"No usable memory was found\r\n");
        while(1);
    }

    //Return the map key
    return map_key;
}

/*
 * Allocate a block of memory
 */
void* malloc(size_t size){
    used_ram_size += size;
    //If we have enough memory in the general
    //  free memory "heap", allocate it there
    if(gen_free_top - gen_free_base >= size){
        void* saved_base = gen_free_base;
        gen_free_base = (uint8_t*)gen_free_base + size;
        return saved_base;
    }
    
    //If we didn't return by this point, we
    //  need to go and find a non-general free block
    //But if there are no such blocks, we do not
    //  have enough memory and need to crash
    if(first_free_block == NULL){
        #ifdef STDLIB_CARSH_ON_ALLOC_ERR
            crash_label_2: gfx_panic((uint64_t)&&crash_label_2, KRNL_PANIC_NOMEM_CODE);
        #else
            return NULL;
        #endif
    }
    free_block_t* current = &(free_block_t){.next = first_free_block};
    while(current = current->next){
        if(current->size + 16 >= size){
            //Link the previous block to the next one
            //  as the current one will not be free soon
            current->prev = current->next;
            //Return the pointer to the block
            return current;
        }
    }
    //If we still didn't find free space, we defenitely don't have enough RAM
    #ifdef STDLIB_CARSH_ON_ALLOC_ERR
        crash_label_3: gfx_panic((uint64_t)&&crash_label_3, KRNL_PANIC_NOMEM_CODE);
    #else
        return NULL;
    #endif
}

/*
 * Free a memory block allocated by malloc(), calloc() and others
 */
void free(void* ptr){
    /*
    //Find a used block with a pointer equal to the provided one
    int32_t blk = -1;
    for(uint32_t i = 0; i < STDLIB_DRAM_MEMBLOCKS; i++){
        if(_mem_blocks[i].used && _mem_blocks[i].ptr == ptr){
            blk = i;
            break;
        }
    }
    //If no such blocks were found, the caller is a LIAR!
    if(blk == -1)
        return;
    //Mark the found block as unused
    _mem_blocks[blk].used = 0;
    //If this isn't the first block, merge it with the previous one if it's unused too
    if(blk > 0){
        if(!_mem_blocks[blk - 1].used){
            struct _mem_block blk_new;
            blk_new.ptr = _mem_blocks[blk - 1].ptr;
            blk_new.size = _mem_blocks[blk - 1].size + _mem_blocks[blk].size;
            blk_new.used = 0;
            _mem_blocks[blk] = blk_new;
            //Mark the previous block as invalid
            _mem_blocks[blk - 1].ptr = NULL;
        }
    }
    //If this isn't the last block, merge it with the next one if it's unused too
    if(blk < STDLIB_DRAM_MEMBLOCKS - 1){
        if(!_mem_blocks[blk + 1].used){
            struct _mem_block blk_new;
            blk_new.ptr = _mem_blocks[blk].ptr;
            blk_new.size = _mem_blocks[blk + 1].size + _mem_blocks[blk].size;
            blk_new.used = 0;
            _mem_blocks[blk] = blk_new;
            //Mark the next block as invalid
            _mem_blocks[blk + 1].ptr = NULL;
        }
    }
    */
}

/*
 * Allocate a block of memory and fill it with zeroes
 */
void* calloc(uint64_t num, size_t size){
    //Allocate the memory using malloc()
    void* malloc_res = malloc(num * size);
    if(malloc_res != NULL) //If the returned pointer isn't null, memset() with zeroes and return it
        return memset(malloc_res, 0, num * size);
    else //Else, return null too
        return NULL;
}

/*
 * Fill a chunk of memory with certain values
 */
void* memset(void* dst, int ch, size_t size){
    //Convert ch to 8 bits
    unsigned char c = (unsigned char)ch;
    //Fill the chunk with them
    while(size--)
        *(unsigned char*)(dst + size) = c;
    //Return dst
    return dst;
}

/*
 * Copy a block of memory
 */
void* memcpy(void* destination, const void* source, size_t num){
    if(num == 0)
        return destination;
    #ifdef STDLIB_MEMCPY_WC
    //Chck if the data is 8-byte aligned
    if(((uint64_t)destination % 8 == 0) && ((uint64_t)source % 8 == 0) && (num % 8 == 0)){
        //Tell the CPU we're gonna access the data soon so it can prefetch it
        __asm__ volatile("prefetchnta %0" : : "m" (source));
        //Copy the data
        __asm__ volatile("_stdlib_memcpy_wc:"
                         "  mov (%%rsi), %%rax;"
                         "  movnti %%rax, (%%rdi);"
                         "  add $8, %%rsi;"
                         "  add $8, %%rdi;"
                         "  loop _stdlib_memcpy_wc;" : : "S" (source), "D" (destination), "c" (num / 8));
        //Return
        return destination;
    }
    #endif
    //Q = 8 bytes at a time
    //D = 4 bytes at a time
    //W = 2 bytes at a time
    //B = 1 byte  at a time
    if(num % 8 == 0)
        __asm__ volatile("rep movsq" : : "D" (destination), "S" (source), "c" (num / 4));
    else if(num % 4 == 0)
        __asm__ volatile("rep movsd" : : "D" (destination), "S" (source), "c" (num / 4));
    else if(num % 2 == 0)
        __asm__ volatile("rep movsw" : : "D" (destination), "S" (source), "c" (num / 2));
    else
        __asm__ volatile("rep movsb" : : "D" (destination), "S" (source), "c" (num));
    return destination;
}

/*
 * Copy a block of memory to an overlapping block of memory
 */
void* memmove(void* dest, const void* src, size_t count){
    if(dest <= src){
        //Use memcpy if the destination is below the source
        return memcpy(dest, src, count);
    } else {
        //Use another, non-optimized algorithm
        for(int i = count; i >= 0; i--)
            *(uint8_t*)((uint8_t*)dest + i) = *(uint8_t*)((uint8_t*)src + i);
        return dest;
    }
}

/*
 * Load Interrupt Descriptor Table
 */
void load_idt(struct idt_desc* idt){
    __asm__("lidt %0" : : "m" (*idt));
}

/*
 * Convert big endian doubleword to little endian one and vice versa
 */
void bswap_dw(int* value){
    __asm__("bswapl %%eax" : "=a" (*value) : "a" (*value));
}

/*
 * Output doubleword to I/O port
 */
void outl(unsigned short port, unsigned int value){
    __asm__ volatile("outl %%eax, %%dx" : : "a" (value), "d" (port));
}

/*
 * Read doubleword from I/O port
 */
unsigned int inl(unsigned short port){
    unsigned int value;
    __asm__ volatile("inl %%dx, %%eax" : "=a" (value) : "d" (port));
    return value;
}

/*
 * Output word to I/O port
 */
void outw(unsigned short port, unsigned short value){
    __asm__ volatile("outw %%ax, %%dx" : : "a" (value), "d" (port));
}

/*
 * Read word from I/O port
 */
unsigned short inw(unsigned short port){
    unsigned short value;
    __asm__ volatile("inw %%dx, %%ax" : "=a" (value) : "d" (port));
    return value;
}

/*
 * Read a number of words from I/O port and store it in memory
 */
void rep_insw(uint16_t port, uint32_t count, uint16_t* buf){
    __asm__ volatile("rep insw" : : "c" (count), "D" (buf), "d" (port));
}

/*
 * Output byte to I/O port
 */
void outb(unsigned short port, unsigned char value){
    __asm__ volatile("outb %%al, %%dx" : : "a" (value), "d" (port));
}

/*
 * Read byte from I/O port
 */
unsigned char inb(unsigned short port){
    unsigned char value;
    __asm__ volatile("inb %%dx, %%al" : "=a" (value) : "d" (port));
    return value;
}

/*
 * Reads a model-specific register (MSR)
 */
uint64_t rdmsr(uint32_t msr){
    uint64_t val_h;
    uint64_t val_l;
    __asm__ volatile("rdmsr" : "=d" (val_h), "=a" (val_l) : "c" (msr));
    return (val_h << 32) | val_l;
}

/*
 * Writes a value to model-specific register (MSR)
 */
void wrmsr(uint32_t msr, uint64_t val){
    uint32_t val_h = val >> 32;
    uint32_t val_l = val & 0xFFFFFFFF;
    __asm__ volatile("wrmsr" : : "d" (val_h), "a" (val_l), "c" (msr));
}

/*
 * Manages pushing bytes into the FIFO buffer
 */
void fifo_pushb(unsigned char* buffer, unsigned short* head, unsigned char value){
    buffer[(*head)++] = value;
}

/*
 * Manages popping bytes from the FIFO buffer
 */
unsigned char fifo_popb(unsigned char* buffer, unsigned short* head, unsigned short* tail){
    unsigned char value = buffer[(*tail)++];
    if(*tail >= *head)
        *tail = *head = 0;
    return value;
}

/*
 * Returns the amount of bytes available for reading in the FIFO buffer
 */
unsigned char fifo_av(unsigned short* head, unsigned short* tail){
    return *head - *tail;
}

/*
 * Append an element at the end of the list
 */
list_node_t* list_append(list_node_t* first, void* element){
    //Allocate a block of memory for the node
    list_node_t* node = (list_node_t*)malloc(sizeof(list_node_t));
    //Assign the element pointer to it
    node->data = element;
    node->next = NULL;
    node->prev = NULL;
    if(first == NULL){
        //If the first element is NULL, we're creating a new list
        return node;
    } else {
        //Else, walk through the list to find its last element
        //  and assign the new one to that one
        //Also, assign that one to the new one as a previous element
        list_node_t* last = first;
        while((last = last->next)->next);
        last->next = node;
        node->prev = last;
        return first;
    }
}

/*
 * Get an element at a specific index from the list
 */
void* list_get_at_idx(list_node_t* first, uint32_t idx){
    uint32_t cnt = 0;
    //Scan through the list
    //    WARNING: TRYING TO UNDERSTAND THE TWO LINES OF CODE LISTED BELOW
    //    MIGHT LEAD TO SERIOUS BRAIN INJURIES.
    list_node_t* last = first;
    while((cnt++ != idx) && (last = last->next));
    //Return the element
    return last->data;
}

/*
 * Read the amount of cycles executed by the CPU
 */
uint64_t rdtsc(void){
    uint32_t h, l;
    __asm__ volatile("rdtsc" : "=d" (h), "=a" (l));
    return (uint64_t)((uint64_t)h << 32) | l;
}

/*
 * Get the length of a zero-terminated string
 */
size_t strlen(const char* str){
    size_t i = 0;
    while(str[i++] != 0);
    return i - 1;
}

/*
 * Read RTC hour, minute, second, day, month and year registers
 * Returns 0 if the data is valid
 */
uint8_t read_rtc_time(uint16_t* h, uint16_t* m, uint16_t* s, uint16_t* d, uint16_t* mo, uint16_t* y){
    //Read CMOS Status Register A
    outb(0x70, 0x0A);
    uint8_t status_a = inb(0x71);
    uint32_t i = 0;
    //If bit 7 is set, we wait for it to reset and then read the time OR 10k iterations and return
    while(status_a & (1 << 7) && (i++ <= 10000)){
        outb(0x70, 0x0A);
        status_a = inb(0x71);
    }
    if(i++ >= 10000)
        return 0;
    //Read hours
    outb(0x70, 0x04);
    *h = inb(0x71);
    //Read minutes
    outb(0x70, 0x02);
    *m = inb(0x71);
    //Read seconds
    outb(0x70, 0x00);
    *s = inb(0x71);
    //Read day
    outb(0x70, 0x07);
    *d = inb(0x71);
    //Read month
    outb(0x70, 0x08);
    *mo = inb(0x71);
    //Read year
    outb(0x70, 0x09);
    *y = inb(0x71);

    //Read Status Register B to find out the data format
    outb(0x70, 0x0B);
    uint8_t status_b = inb(0x71);
    //If bit 2 is set, the values are BCD
    if(!(status_b & (1 << 2))){
        *h = (((*h >> 4) & 0x0F) * 10) + (*h & 0x0F);
        *m = (((*m >> 4) & 0x0F) * 10) + (*m & 0x0F);
        *s = (((*s >> 4) & 0x0F) * 10) + (*s & 0x0F);
        *d = (((*d >> 4) & 0x0F) * 10) + (*d & 0x0F);
        *mo = (((*mo >> 4) & 0x0F) * 10) + (*mo & 0x0F);
        *y = (((*y >> 4) & 0x0F) * 10) + (*y & 0x0F);
    }
    //If bit 1 is set, the time is in the 12-hour format
    //  add 12 to the hour count if its highest bit is set
    if(status_b & (1 << 1)){
        if(*h & (1 << 7)){
            *h += 12;
            //Reset the highest bit as this bit set will now mess with the value
            *h &= ~(1 << 7);
        }
    }

    //We're in the 21st century after all
    *y += 2000;

    //Success!
    return 1;
}

/*
 * Print an uint64_t to the string
 */
char* sprintu(char* str, uint64_t i, uint8_t min){
    //Create some variables
    uint8_t pos = 0;
    uint64_t div = 1000000000000000000; //Start with the leftmost digit
    uint8_t started = 0;
    for(int j = 1; j <= 19; j++){
        //Fetch the next digit
        uint8_t digit = (i / div) % 10;
        //If the conversion hasn't started already and the current digit
        //  is greater than zero OR we exceeded the maximum amount of dropped
        //  digits, assume that the conversion has started
        if((!started && digit > 0) || (19 - j < min))
            started = 1;
        //If the conversion has started, write a digit to the string
        if(started)
            str[pos++] = digit + '0';
        //Move to the next digit
        div /= 10;
    }
    //Mark the end of the string
    str[pos] = 0;
    //Return the string
    return str;
}

char hex_const[16] = "0123456789ABCDEF";

/*
 * Print an uint64_t with base 16 to the string
 */
char* sprintub16(char* str, uint64_t i, uint8_t min){
    //Create some variables
    uint8_t pos = 0;
    uint64_t div = 1ULL << 60; //Start with the leftmost digit
    uint8_t started = 0;
    for(uint8_t j = 1; j <= 16; j++){
        //Fetch the next digit
        uint8_t digit = (i / div) % 16;
        //If the conversion hasn't started already and the current digit
        //  is greater than zero OR we exceeded the maximum amount of dropped
        //  digits, assume that the conversion has started
        if((!started && digit > 0) || (16 - j < min))
            started = 1;
        //If the conversion has started, write a digit to the string
        if(started)
            str[pos++] = hex_const[digit];
        //Move to the next digit
        div >>= 4;
    }
    //Mark the end of the string
    str[pos] = 0;
    //Return the string
    return str;
}

/*
 * Append the string at src to the end of the string at dest
 */
char* strcat(char* dest, char* src){
    //Get the lengths of the strings
    size_t dest_len = strlen(dest);
    size_t src_len = strlen(src);
    //Copy the source string
    memcpy((void*)(dest + dest_len), (const void*)src, src_len);
    //Mark the end
    dest[dest_len + src_len] = 0;
    //Return the destination
    return dest;
}

/*
 * Compare two memory blocks
 */
int memcmp(const void* lhs, const void* rhs, size_t cnt){
    //Go through each byte
    for(int i = 0; i < cnt; i++){
        //Return if the blocks aren't equal
        if(((const uint8_t*)lhs)[i] > ((const uint8_t*)rhs)[i])
            return 1;
        else if(((const uint8_t*)lhs)[i] < ((const uint8_t*)rhs)[i])
            return -1;
    }
    //If we didn't return, the blocks are equal
    return 0;
}

/*
 * Compare two zero-terminated strings
 */
int strcmp(const char* str1, const char* str2){
    //Calculate the length of both strings
    int len1 = strlen(str1);
    int len2 = strlen(str2);
    //Find the minimal one
    int min_len = (len1 < len2) ? len1 : len2;
    //Go through each byte
    for(int i = 0; i < min_len; i++){
        //Return if the strings aren't equal
        if(str1[i] > str2[i])
            return 1;
        else if(str1[i] < str2[i])
            return -1;
    }
    //If we didn't return, the strings are equal
    return 0;
}

/*
 * Create a GDT entry
 */
void gdt_create(uint16_t sel, uint32_t base, uint32_t limit, uint8_t flags, uint8_t access){
    //Calculate the entry base pointer
    struct idt_desc desc;
    __asm__ volatile("sgdt %0" : : "m" (desc)); //Store the GDT descriptor
    void* entry_ptr = desc.base + (sel * 8);

    //Set limit[15:0]
    *(uint16_t*)(entry_ptr + 0) = limit & 0xFFFF;
    //Set base[15:0]
    *(uint16_t*)(entry_ptr + 2) = base & 0xFFFF;
    //Set base[23:16]
    *(uint16_t*)((uint8_t*)entry_ptr + 4) = (base >> 16) & 0xFF;
    //Set access byte
    *(uint8_t*)(entry_ptr + 5) = access;
    //Set limit[19:16]
    *(uint8_t*)(entry_ptr + 6) = (limit >> 16) & 0xF;
    //Set flags
    *(uint8_t*)(entry_ptr + 6) |= (flags & 0xF) << 4;
    //Set base[31:24]
    *(uint8_t*)(entry_ptr + 4) = (base >> 24) & 0xFF;

    //Load GDT
    if(desc.limit < sel * 8) //Increase the limit if it won't fit
        desc.limit = sel * 8;
    __asm__ volatile("lgdt %0" : : "m" (desc));
}