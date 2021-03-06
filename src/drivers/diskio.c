//Neutron Project
//General disk I/O

#include "./diskio.h"
#include "../stdlib.h"
#include "./ata.h"
#include "./gfx.h"

disk_part_t* partitions;
uint8_t* buffer;

/*
 * Initialize the disk I/O subsystem by initializing all known storage devices and loading all the partitions from them
 */
void diskio_init(void){
    //Allocate a chunk of memory for the list
    partitions = (disk_part_t*)malloc(4 * 4 * sizeof(disk_part_t));
    uint16_t cur_part = 0;
    //A dummy partition
    disk_part_t* dummy_part;
    //Allocate a chunk of memory as a read/write buffer
    buffer = (uint8_t*)malloc(DISK_IO_BUFFER_SIZE);
    //Go through the ATA devices
    for(uint8_t i = 0; i < 4; i++){
        char temp[50] = "Detecting partitions on ATA drive ";
        char temp2[15];
        strcat(temp, sprintu(temp2, i, 1));
        gfx_verbose_println(temp);
        //Get the device type
        uint8_t ata_type = ata_get_type(i >> 1, i & 1);
        //We only support PATA for now
        if(ata_type == ATA_DEV_PATA){
            //Read the device's first sector that contains MBR
            ata_read_sect(i >> 1, i & 1, 0, 1, buffer);
            //Go through all the partition entries
            for(uint8_t p = 0; p < 4; p++){
                uint64_t p_base = (uint64_t)buffer + 0x1BE + (p * 16);
                //Write the partition disk type, number and MBR entry number
                partitions[cur_part].device_type = DISK_PATA;
                partitions[cur_part].device_no = i;
                partitions[cur_part].mbr_entry_no = p;
                //Fetch the partition type
                partitions[cur_part].type = *(uint8_t*)(p_base + 4);
                //Fetch the partition start sector
                partitions[cur_part].lba_start = *(uint32_t*)(p_base + 8);
                //Fetch the partition length
                partitions[cur_part].size = *(uint32_t*)(p_base + 0xC);
                //Fetch the partition status
                partitions[cur_part].valid = (partitions[cur_part].type != 0);
                //Increment the partition pointer
                if(partitions[cur_part].valid){
                    //Print the partition info
                    temp[0] = 0;
                    strcat(temp, "Found MBR partition type ");
                    strcat(temp, sprintu(temp2, partitions[cur_part].type, 1));
                    gfx_verbose_println(temp);
                    //Go to the next one
                    cur_part++;
                }
            }
        }
    }
}

/*
 * Read sectors to the buffer from a partition
 */
void diskio_read_sect(uint16_t part_no, uint32_t sect, uint8_t count, uint8_t from_part){
    disk_part_t* part = &partitions[part_no];
    //Immediately return if the partition is invalid
    if(!part->valid)
        return;
    //Depending on the disk type, read sectors
    switch(part->device_type){
        case DISK_PATA:
            //from_part=1 means sector number is with respect to partition start
            ata_read_sect(part->device_no >> 1, part->device_no & 1,
                          sect + (from_part ? part->lba_start : 0), count, buffer);
            break;
    }
}

/*
 * Read a file from disk
 */
uint8_t diskio_fs_read_file(uint8_t part_no, char* name, uint8_t* dest_buffer){
    disk_part_t* part = &partitions[part_no];
    //Immediately return if the partition is invalid
    if(!part->valid)
        return DISKIO_STATUS_INVALID_PART;
    //Everything depends on the filesystem
    //Neutron currently only supports nFS
    if(part->type == 0x58){
        //Read nFS master sector
        diskio_read_sect(part_no, 0, 1, 1);
        //Check nFS signature
        if(*(uint32_t*)(buffer) != 0xDEADF500)
            return DISKIO_STATUS_INVALID_PART;
        //Read nFS master filetable
        diskio_read_sect(part_no, 1, 1, 1);
        //Go through all the file entries
        for(uint16_t i = 0; i < 512; i += 32){
            //Fetch the filename
            char* filename = (char*)(buffer + i);
            //Compare it with the desired name
            if(strcmp(filename, name) == 0){
                //Fetch file's length
                uint32_t fsize = *(uint32_t*)(buffer + i + 24);
                //Calculate file length in sectors
                uint32_t fsize_sect = fsize / 512;
                if(fsize % 512 > 0)
                    fsize_sect++;
                //Fetch file's starting LBA
                uint32_t fslba = *(uint32_t*)(buffer + i + 28);
                //Read the sectors
                diskio_read_sect(part_no, fslba, fsize_sect, 0);
                //Copy them over to the destination buffer
                memcpy(dest_buffer, buffer, fsize);
                //Return
                return DISKIO_STATUS_OK;
            }
        }
        //If no file with such a name was found, return
        return DISKIO_STATUS_FILE_NOT_FOUND;
    } else {
        return DISKIO_STATUS_UNSUPPORTED_FS;
    }
}