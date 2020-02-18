//Neutron Project
//Initrd driver
//  (initialization ramdisk)

#include "./initrd.h"
#include "../stdlib.h"

#include <efi.h>
#include <efilib.h>
#include <efiprot.h>

EFI_SYSTEM_TABLE* krnl_get_efi_systable(void);

//Initrd image
uint8_t* initrd_raw;
uint64_t initrd_size;

//Root EFI file protocol
EFI_FILE_PROTOCOL* root_file_prot;
//Initrd EFI file protocol
EFI_FILE_PROTOCOL* initrd_file_prot;

/*
 * Loads INITRD file from disk
 * Returns 0 on success, other on failure
 */
uint8_t initrd_init(void){
    //Try to find the SFS protocol
    EFI_STATUS status;
    EFI_HANDLE* handles = NULL;
    uint64_t handle_count = 0;
    status = krnl_get_efi_systable()->BootServices->LocateHandleBuffer(ByProtocol, &((EFI_GUID)EFI_SIMPLE_FILE_SYSTEM_PROTOCOL_GUID),
        NULL, &handle_count, &handles);
    if(EFI_ERROR(status))
        return 1;
    //Scan through handles
    for(int i = 0; i < handle_count; i++){
        //Find the SFS protocol
        EFI_SIMPLE_FILE_SYSTEM_PROTOCOL* sfs_prot;
        status = krnl_get_efi_systable()->BootServices->HandleProtocol(handles[i], &((EFI_GUID)EFI_SIMPLE_FILE_SYSTEM_PROTOCOL_GUID), (void**)&sfs_prot);
        if(EFI_ERROR(status))
            return 2;
        //Try to find the file protocol from that
        status = sfs_prot->OpenVolume(sfs_prot, &root_file_prot);
        if(EFI_ERROR(status))
            return 2;
        //Open the initrd file on it
        status = root_file_prot->Open(root_file_prot, &initrd_file_prot, (CHAR16*)L"EFI\\nOS\\initrd", EFI_FILE_MODE_READ, 0);
        if(EFI_ERROR(status))
            continue; //Skip this partition on error
        //Read its info
        EFI_FILE_INFO info;
        uint64_t size = sizeof(EFI_FILE_INFO) * 10;
        status = initrd_file_prot->GetInfo(initrd_file_prot, &((EFI_GUID)EFI_FILE_INFO_ID), &size, (void*)&info);
        if(EFI_ERROR(status))
            return 4;
        //Get the size and allocate the buffer
        initrd_size = info.FileSize;
        initrd_raw = (uint8_t*)malloc(initrd_size);
        //Read the file
        status = initrd_file_prot->Read(initrd_file_prot, &initrd_size, (void*)initrd_raw);
        if(EFI_ERROR(status))
            return 5;
        //Close both files
        initrd_file_prot->Close(initrd_file_prot);
        root_file_prot->Close(root_file_prot);
        return 0;
    }
    //No initrd file on all partitions
    return 3;
}

/*
 * Reads file info from INITRD
 */
initrd_file_t initrd_read(char* name){
    uint32_t i = 0;
    initrd_file_t cur;
    //Scan through the file list
    //Location = 0 means the end of the list
    while(cur.location != 0){
        //Load the dedscriptor
        cur = ((initrd_file_t*)initrd_raw)[i++];
        //Check the name
        if(strcmp(name, cur.name) == 0)
            break;
    }
    return cur;
}

/*
 * Returns pointer to file contents from INITRD
 */
uint8_t* initrd_contents(char* name){
    uint32_t location = initrd_read(name).location;
    return (location == 0) ? NULL : (initrd_raw + location);
}