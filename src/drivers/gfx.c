//Neutron Project
//Kernel graphics driver

#include <efi.h>
#include <efilib.h>
#include <efiprot.h>

#include "./gfx.h"
#include "../stdlib.h"

EFI_SYSTEM_TABLE* krnl_get_efi_systable(void);

//The video buffer pointers
color32_t* vbe_buffer;
color32_t* sec_buffer;
#ifdef GFX_TRIBUF
color32_t* mid_buffer;
#endif
//The resolution
uint32_t res_x;
uint32_t res_y;
//The font
const uint8_t* font;
//The buffer selected for operations
uint8_t buf_sel;
//Is gfx_verbose_println() enabled or not?
uint8_t verbose_enabled;
//Pointer to the graphics output protocol
EFI_GRAPHICS_OUTPUT_PROTOCOL* graphics_output = NULL;
//Pointer to the UGA protocol
//EFI_UGA_DRAW_PROTOCOL* graphics_uga = NULL;

/*
 * Retrieve the horizontal resolution
 */
uint32_t gfx_res_x(void){
    return res_x;
}

/*
 * Retrieve the vertical resolution
 */
uint32_t gfx_res_y(void){
    return res_y;
}

/*
 * Retrieve the currently used buffer pointer
 */
color32_t* gfx_buffer(void){
    return (buf_sel == GFX_BUF_VBE) ? vbe_buffer : sec_buffer;
}

color32_t* gfx_buf_another(void){
    return (buf_sel == GFX_BUF_VBE) ? sec_buffer : vbe_buffer;
}

/*
 * Initialize the graphics driver
 */
void gfx_init(void){
    //Find the graphics output protocol
    gfx_find_gop();
    //If it hadn't been found, print an error
    if(graphics_output == NULL){
        krnl_get_efi_systable()->ConOut->OutputString(krnl_get_efi_systable()->ConOut,
            (CHAR16*)L"Unable to find the graphics output protocol. Trying UGA instead\r\n");
        //gfx_find_uga();
        //If no, print an error
        /*
        if(graphics_uga == NULL){
            krnl_get_efi_systable()->ConOut->OutputString(krnl_get_efi_systable()->ConOut,
                (CHAR16*)L"Error: no way to display graphics was detected. If you are sure that your computer supports graphics, report this error\r\n");
            while(1);
        } else {
            krnl_get_efi_systable()->ConOut->OutputString(krnl_get_efi_systable()->ConOut,
                (CHAR16*)L"UGA found\r\n");
        }
        */
    } else {
        krnl_get_efi_systable()->ConOut->OutputString(krnl_get_efi_systable()->ConOut,
            (CHAR16*)L"GOP found\r\n");
    }
    //Choose the best video mode
    gfx_choose_best();
    //Allocate the second buffer based on the screen size
    //  (actually, a little bit bigger than that)
    sec_buffer = (color32_t*)malloc(res_x * (res_y + 16) * sizeof(color32_t));
    #ifdef GFX_TRIBUF
    //Allocate the third buffer
    mid_buffer = (color32_t*)malloc(res_x * res_y * sizeof(color32_t));
    #endif
}

/*
 * Finds the EFI UGA protocol
 */
void gfx_find_uga(void){
    //Locate the protocol
    //Firstly, through the ConsoleOut handle
    /*
    EFI_STATUS status;
    status = krnl_get_efi_systable()->BootServices->HandleProtocol( krnl_get_efi_systable()->ConsoleOutHandle,
        &((EFI_GUID)EFI_UGA_DRAW_PROTOCOL_GUID), (void**)graphics_uga);
    if(!EFI_ERROR(status))
        return;
    //Then, directly
    status = krnl_get_efi_systable()->BootServices->LocateProtocol(&((EFI_GUID)EFI_UGA_DRAW_PROTOCOL_GUID), NULL,
        (void**)&graphics_uga);
    if(!EFI_ERROR(status))
        return;
    //Lastly, locate by handle
    uint64_t handle_count = 0;
    EFI_HANDLE* handle_buf;
    */

}

/*
 * Finds the EFI Graphics Output Protocol
 */
void gfx_find_gop(void){
    //Handle the graphics output protocol
    //Firstly, through the ConsoleOut handle
    EFI_STATUS status;
    status = krnl_get_efi_systable()->BootServices->HandleProtocol(krnl_get_efi_systable()->ConsoleOutHandle,
        &((EFI_GUID)EFI_GRAPHICS_OUTPUT_PROTOCOL_GUID), (void**)&graphics_output);
    if(!EFI_ERROR(status))
        return;
    //Then, directly
    status = krnl_get_efi_systable()->BootServices->LocateProtocol(&((EFI_GUID)EFI_GRAPHICS_OUTPUT_PROTOCOL_GUID),
        NULL, (void**)graphics_output);
    if(!EFI_ERROR(status))
        return;
    //Lastly, locate by handle
    uint64_t handle_count = 0;
    EFI_HANDLE* handle;
    status = krnl_get_efi_systable()->BootServices->LocateHandleBuffer(ByProtocol,
        &((EFI_GUID)EFI_GRAPHICS_OUTPUT_PROTOCOL_GUID), NULL, &handle_count, &handle);
    if(EFI_ERROR(status))
        return;
    for(int i = 0; i < handle_count; i++){
        status = krnl_get_efi_systable()->BootServices->HandleProtocol(handle[i],
            &((EFI_GUID)EFI_GRAPHICS_OUTPUT_PROTOCOL_GUID), (void*)&graphics_output);
        if(EFI_ERROR(status))
            return;
    }
}

/*
 * Chooses the best available video mode
 */
void gfx_choose_best(void){
    EFI_STATUS status;

    krnl_get_efi_systable()->ConOut->OutputString(krnl_get_efi_systable()->ConOut,
        (CHAR16*)L"Probing video mode list\r\n");

    uint32_t mon_best_res_x = 0;
    uint32_t mon_best_res_y = 0;
    //Get the EDID
    uint64_t handle_count = 0;
    EFI_HANDLE* handle;
    EFI_EDID_DISCOVERED_PROTOCOL* edid;
    status = krnl_get_efi_systable()->BootServices->LocateHandleBuffer(ByProtocol,
        &((EFI_GUID)EFI_EDID_DISCOVERED_PROTOCOL_GUID), NULL, &handle_count, &handle);
    if(!EFI_ERROR(status)){
        status = krnl_get_efi_systable()->BootServices->HandleProtocol(handle,
            &((EFI_GUID)EFI_EDID_DISCOVERED_PROTOCOL_GUID), (void*)&edid);
        //Parse it
        //To be specific, its detailed timing descriptors
        //Go through them
        if(!EFI_ERROR(status)){
            for(uint32_t base = 54; base <= 108; base += 18){
                uint32_t mon_res_x = (uint32_t)*(uint8_t*)(edid->Edid + base + 2) << 4;
                uint32_t mon_res_y = (uint32_t)*(uint8_t*)(edid->Edid + base + 5) << 4;
                if(mon_res_x > mon_best_res_x || mon_res_y > mon_best_res_y){
                    mon_best_res_x = mon_res_x;
                    mon_best_res_y = mon_res_y;
                }
            }
        }
    } else {
        mon_best_res_x = 10000;
        mon_best_res_y = 10000;
    }

    EFI_GRAPHICS_OUTPUT_MODE_INFORMATION* mode_info;
    uint64_t mode_info_size;
    uint32_t best_res_x;
    uint32_t best_res_y;
    uint32_t best_mode_num;
    //Go through each mode and query its properties
    for(uint32_t i = 0; i < graphics_output->Mode->MaxMode; i++){
        //Query mode information
        status = graphics_output->QueryMode(graphics_output, i, &mode_info_size, &mode_info);
        //If we encounter an error
        if(EFI_ERROR(status)){
            //If the error is "not started"
            if(status == EFI_NOT_STARTED){
                //Start it and query mode info once again
                graphics_output->SetMode(graphics_output, graphics_output->Mode->Mode);
                graphics_output->QueryMode(graphics_output, i, &mode_info_size, &mode_info);
            } else {
                //In case of any other error, skip this mode
                continue;
            }
        }

        //Only choose RGBReserved modes
        if(mode_info->PixelFormat == PixelRedGreenBlueReserved8BitPerColor){
            //Fetch mode resolution and compare it with the best one currently discovered
            //Do not exceed the display resolution
            uint32_t mode_res_x = mode_info->HorizontalResolution;
            uint32_t mode_res_y = mode_info->VerticalResolution;
            if((mode_res_x > best_res_x || mode_res_y > best_res_y) &&
               mode_res_y <= mon_best_res_y && mode_res_x <= mon_best_res_x){
                //Record the new best mode
                best_res_y = mode_res_y;
                best_res_x = mode_res_x;
                best_mode_num = i;
            }
        }
    }
    //Set the mode
    graphics_output->SetMode(graphics_output, best_mode_num);
    //Retrieve its parameters
    res_x = graphics_output->Mode->Info->HorizontalResolution;
    res_y = graphics_output->Mode->Info->VerticalResolution;
    vbe_buffer = (color32_t*)graphics_output->Mode->FrameBufferBase;
}

/*
 * Transfer the data from the current buffer to the opposing buffer
 */
void gfx_flip(void){
    //Choose the source and destination buffers
    color32_t* buf_src = (buf_sel == GFX_BUF_VBE) ? vbe_buffer : sec_buffer;
    color32_t* buf_dst = (buf_sel == GFX_BUF_VBE) ? sec_buffer : vbe_buffer;
    #ifdef GFX_TRIBUF
    //For each line in the source buffer
    for(uint32_t y = 0; y < res_y; y++){
        uint64_t offs_pix = y * res_x;
        //Compare the lines
        uint8_t need_transfer = memcmp(&buf_src[offs_pix], &mid_buffer[offs_pix], res_x * sizeof(color32_t)) != 0;
        if(need_transfer){
            //If the lines differ, copy them
            memcpy(&mid_buffer[offs_pix], &buf_src[offs_pix],res_x * sizeof(color32_t));
            memcpy(&buf_dst[offs_pix], &buf_src[offs_pix], res_x * sizeof(color32_t));
        }
    }
    #else
    //Transfer the buffer
    memcpy(buf_dst, buf_src, res_x * res_y * sizeof(color32_t));
    #endif
}

/*
 * Set the font that will be used in future
 */
void gfx_set_font(const unsigned char* fnt){
    font = fnt;
}

/*
 * Set the video buffer that will be used in future
 */
void gfx_set_buf(unsigned char buf){
    //Abort if buffer is invalid
    if(buf != GFX_BUF_VBE && buf != GFX_BUF_SEC)
        abort();
    //Assign the buffer otherwise
    buf_sel = buf;
}

/*
 * Fill the screen with one color
 */
void gfx_fill(color32_t color){
    //Get the video buffer
    color32_t* buf = gfx_buffer();
    //Draw each pixel
    for(uint32_t i = 0; i < res_x * res_y; i++)
        buf[i] = color;
}

/*
 * Draw a filled rectangle
 */
void gfx_draw_filled_rect(p2d_t pos, p2d_t size, color32_t c){
    //Draw each horizontal line in the rectangle
    for(uint64_t y = pos.y; y < pos.y + size.y; y++)
        gfx_draw_hor_line((p2d_t){.x = pos.x, .y = y}, size.x, c);
}

/*
 * Draw a blurred rectangle
 */
void gfx_draw_blurred_rect(p2d_t pos, p2d_t size, color32_t c){
    //Create a temporary buffer
    color32_t buf[size.x * size.y];
    //Go through each pixel in the temporary buffer
    for(uint32_t y = 0; y < size.y; y++){
        for(uint32_t x = 0; x < size.x; x++){
            //Get the original color
            color32_t col = gfx_buffer()[((pos.y + y) * res_x) + pos.x + x];
            //And mix it with the neighboring ones
            for(uint32_t y_m = y - 1; y_m <= y + 1; y_m++)
                for(uint32_t x_m = x - 1; x_m <= x + 1; x_m++)
                    col = gfx_blend_colors(col, gfx_buffer()[((pos.y + y_m) * res_x) + pos.x + x_m], 128);
            //Color it
            col = gfx_blend_colors(col, c, c.a);
            //Apply the pixel to the temporary buffer
            buf[(y * size.x) + x] = col;
        }
    }
    //Copy the temporary buffer to the main one
    for(uint32_t y_offs = 0; y_offs < size.y; y_offs++)
        memcpy(&gfx_buffer()[((pos.y + y_offs) * res_x) + pos.x], &buf[y_offs * size.x], size.x * sizeof(color32_t));
}

/*
 * Draw a rectangle
 */
void gfx_draw_rect(p2d_t pos, p2d_t size, color32_t c){
    //Draw two horizontal lines
    gfx_draw_hor_line((p2d_t){.x = pos.x, .y = pos.y}, (uint64_t)size.x, c);
    gfx_draw_hor_line((p2d_t){.x = pos.x, .y = pos.y + size.y}, (uint64_t)size.x, c);
    //Draw two vertical lines
    gfx_draw_vert_line((p2d_t){.x = pos.x, .y = pos.y}, (uint64_t)size.y, c);
    gfx_draw_vert_line((p2d_t){.x = pos.x + size.x, .y = pos.y}, (uint64_t)size.y, c);
}

/*
 * Draw a horizontal line
 */
void gfx_draw_hor_line(p2d_t pos, uint64_t w, color32_t c){
    //Get the video buffer
    color32_t* buf = gfx_buffer();
    //Calculate the scanline start
    uint64_t st = pos.y * res_x;
    //Draw each pixel in the line
    for(uint64_t x = pos.x; x < pos.x + w; x++)
        buf[st + x] = gfx_blend_colors(buf[st + x], c, c.a);
}

/*
 * Draw a vertical line
 */
void gfx_draw_vert_line(p2d_t pos, uint64_t h, color32_t c){
    //Get the video buffer
    color32_t* buf = gfx_buffer();
    //Calculate the scanline start
    uint32_t st = (pos.y * res_x) + pos.x;
    //Draw each pixel in the line
    for(uint64_t o = 0; o <= h * res_x; o += res_x)
        buf[st + o] = gfx_blend_colors(buf[st + o], c, c.a);
}

/*
 * Draw an XBM image
 */
void gfx_draw_xbm(p2d_t position, uint8_t* xbm_ptr, p2d_t xbm_size, color32_t color_h, color32_t color_l){
    //Create some local variables
    uint8_t* ptr = xbm_ptr;
    p2d_t pos = position;
    color32_t* buffer = gfx_buffer();
    while(1){
        //Fetch a byte
        uint8_t data = *(ptr++);
        //Cycle through its bits
        for(uint16_t x = 0; x < 8; x++){
            //Check the position
            if(pos.x - position.x < xbm_size.x){
                //If it is in bounds, draw the pixel
                if(((data >> x) & 1) && color_h.a != 0)
                    buffer[(pos.y * res_x) + pos.x] = color_h;
                else if(color_l.a != 0)
                    buffer[(pos.y * res_x) + pos.x] = color_l;
            }
            //Increment the position
            pos.x++;
        }
        //If the X coordinate has reached the limit, reset it and increment the Y coordinate
        if(pos.x - position.x >= xbm_size.x){
            pos.x = position.x;
            pos.y++;
        }
        //If the Y coordinate has reached the limit, return
        if(pos.y - position.y >= xbm_size.y)
            return;
    }
}

/*
 * Blends from background to foreground colors using the alpha value
 */
color32_t gfx_blend_colors(color32_t b, color32_t f, uint8_t a){
    if(a == 0)
        return b;
    if(a == 255)
        return f;
    return COLOR32(255, ((((int32_t)f.r - (int32_t)b.r) * (int32_t)a) / 255) + (int32_t)b.r,
                        ((((int32_t)f.g - (int32_t)b.g) * (int32_t)a) / 255) + (int32_t)b.g,
                        ((((int32_t)f.b - (int32_t)b.b) * (int32_t)a) / 255) + (int32_t)b.b);
}

/*
 * Draw a raw image
 */
void gfx_draw_raw(p2d_t position, uint8_t* raw_ptr, p2d_t raw_size){
    //Get the buffer
    color32_t* buf = gfx_buffer();
    //Create a counter
    uint32_t pos = 0;
    //Go through each pixel
    for(uint32_t y = position.y; y < position.y + raw_size.y; y++){
        for(uint32_t x = position.x; x < position.x + raw_size.x; x++){
            //Fetch the data
            uint8_t r = raw_ptr[pos++];
            uint8_t g = raw_ptr[pos++];
            uint8_t b = raw_ptr[pos++];
            uint8_t a = raw_ptr[pos++];
            //Draw the pixel
            if(a != 0)
                buf[(y * res_x) + x] = gfx_blend_colors(buf[(y * res_x) + x], COLOR32(255, r, g, b), a);
        }
    }
}

/*
 * Draw an alpha-key image
 */
void gfx_draw_raw_key(p2d_t position, uint8_t* raw_ptr, p2d_t raw_size, color32_t color, uint8_t rotate){
    //Get the buffer
    color32_t* buf = gfx_buffer();
    //Create a counter
    uint32_t pos = 0;
    //Determine the initial value and end value
    int32_t init_x = (rotate == GFX_ROT_CW_0 || rotate == GFX_ROT_CW_90) ? position.x : (position.x + raw_size.x);
    int32_t init_y = (rotate == GFX_ROT_CW_0 || rotate == GFX_ROT_CW_270) ? position.y : (position.y + raw_size.y);
    int32_t end_x = (rotate == GFX_ROT_CW_0 || rotate == GFX_ROT_CW_90) ? (position.x + raw_size.x) : position.x;
    int32_t end_y = (rotate == GFX_ROT_CW_0 || rotate == GFX_ROT_CW_270) ? (position.y + raw_size.y) : position.y;
    //Go through each pixel
    //Don't be scared...
    //We are just determining the step based on which number is larger, end or start
    for(uint32_t y = init_y; (end_y > init_y) ? (y < end_y) : (y > end_y); (end_y > init_y) ? y++ : y--){
        for(uint32_t x = init_x; (end_x > init_x) ? (x < end_x) : (x > end_x); (end_x > init_x) ? x++ : x--){
            //Fetch the data
            pos += 3;
            uint8_t a = raw_ptr[pos++];
            //Draw the pixel
            if(a != 0)
                buf[(y * res_x) + x] = gfx_blend_colors(buf[(y * res_x) + x], color, a);
        }
    }
}

/*
 * Put a char with backgrund color in video buffer, return the size of the draw character
 */
p2d_t gfx_putch(p2d_t pos, color32_t color, color32_t bcolor, char c){
    //Get the video buffer
    color32_t* buf = gfx_buffer();
    //Calculate the video buffer offset
    uint64_t buf_offset = (pos.y * res_x) + pos.x;
    //Calculate the font offset
    uint64_t font_offset = (c - 1) * 6;
    //For keeping track of the character width
    uint32_t c_width = 0;
    //For each column in the font
    for(uint32_t i = 0; i < 6; i++){
        //Load it
        uint8_t font_col = font[font_offset + i];
        //And for each pixel in that column
        for(uint8_t j = 0; j < 8; j++){
            if((font_col >> j) & 1)
                buf[buf_offset + i + (j * res_x)] = gfx_blend_colors(buf[buf_offset + i + (j * res_x)], color, color.a);
            else
                buf[buf_offset + i + (j * res_x)] = gfx_blend_colors(buf[buf_offset + i + (j * res_x)], bcolor, bcolor.a);
            /*
            if(((font_col >> j) & 1) && (color.a != 0))
                buf[buf_offset + i + (j * res_x)] = color; //Draw it
            else if(bcolor.a != 0)
                buf[buf_offset + i + (j * res_x)] = bcolor; //Or clear it
                */
        }
        //Increment the character width
        c_width++;
        //Return early if the end had been reached
        if(i > 2 && font_col == 0)
            return (p2d_t){c_width, 8};
    }
    return (p2d_t){c_width, 8};
}

/*
 * Put a string in video buffer
 */
void gfx_puts(p2d_t pos, color32_t color, color32_t bcolor, char* s){
    //Data byte, position, state and counter
    char c = 0;
    p2d_t pos_actual = pos;
    uint32_t state = 0;
    uint32_t i = 0;
    //Fetch the next character
    while((c = s[i++]) != 0){
        if(state == 0){ //Normal state
            //Process control characters
            switch(c){
                case '\n': //Carriage return
                    pos_actual.x = pos.x;
                    pos_actual.y += 8;
                    break;
                default: { //Print the char and advance its position
                    p2d_t char_size = gfx_putch(pos_actual, color, bcolor, c);
                    pos_actual.x += char_size.x;
                    break;
                }
            }
        }
    }
}

/*
 * Calculate the bounds of a string if it was rendered on screen
 */
p2d_t gfx_text_bounds(char* s){
    char c = 0;
    p2d_t sz = (p2d_t){.x = 0, .y = 8};
    p2d_t pos = (p2d_t){.x = 0, .y = 0};
    uint32_t state = 0;
    uint32_t i = 0;
    //Fetch the next character
    while((c = s[i++]) != 0){
        //Process control characters
        switch(c){
            case '\n': //Carriage return
                pos.x = 0;
                pos.y += 8;
                break;
            default: { //Advance the position
                //Get the character size by printing it with alpha=0
                p2d_t char_size = gfx_putch((p2d_t){0, 0}, COLOR32(0, 0, 0, 0), COLOR32(0, 0, 0, 0), c);
                pos.x += char_size.x;
                break;
            }
        }
        //If the X position is greater than the X size, update the latter
        if(pos.x > sz.x)
            sz.x = pos.x;
        //The same thing with the Y position
        if(pos.y > sz.y)
            sz.y = pos.y;
    }
    //Return the size
    return sz;
}

/*
 * Draw a panic screen
 */
void gfx_panic(uint64_t ip, uint64_t code){
    //Flip the main buffer to the working one
    gfx_set_buf(GFX_BUF_VBE);
    gfx_flip();
    gfx_set_buf(GFX_BUF_SEC);
    //Darken the screen
    color32_t* buf = gfx_buffer();
    uint32_t res_x = gfx_res_x();
    uint32_t res_y = gfx_res_y();
    for(uint32_t y = 0; y < res_y; y++){
        for(uint32_t x = 0; x < res_x; x++){
            color32_t orig = buf[(y * res_x) + x];
            //But while giving a red tint to it
            buf[(y * res_x) + x] = gfx_blend_colors(orig, COLOR32(255, 128, 0, 0), 64);
        }
    }
    //Determine the error message that needs to be printed
    char* panic_msg = NULL;
    if(code == KRNL_PANIC_NOMEM_CODE)
        panic_msg = KRNL_PANIC_NOMEM_MSG;
    else if(code == KRNL_PANIC_PANTEST_CODE)
        panic_msg = KRNL_PANIC_PANTEST_MSG;
    else if((code & 0xFF) == KRNL_PANIC_CPUEXC_CODE)
        panic_msg = KRNL_PANIC_CPUEXC_MSG;
    else if(code == KRNL_PANIC_STACK_SMASH_CODE)
        panic_msg = KRNL_PANIC_STACK_SMASH_MSG;
    else
        panic_msg = KRNL_PANIC_UNKNOWN_MSG;
    //Construct the error message
    char text[300];
    char temp[20];
    text[0] = 0;
    strcat(text, "Kernel panic occured at address 0x");
    strcat(text, sprintub16(temp, ip, 16));
    strcat(text, "\nerrcode ");
    strcat(text, sprintu(temp, code & 0xFF, 1));
    strcat(text, ": ");
    strcat(text, panic_msg);
    //Add a line for CPU exceptions
    if((code & 0xFF) == KRNL_PANIC_CPUEXC_CODE){
        strcat(text, "\nCPU exc. ");
        strcat(text, sprintu(temp, (code >> 8) & 0xFF, 2));
        strcat(text, " ex. data ");
        strcat(text, sprintu(temp, (code >> 16) & 0xFFFFFFFF, 2));
    }
    //Get its bounds
    p2d_t msg_bounds = gfx_text_bounds(text);
    //Print it
    gfx_puts((p2d_t){.x = (gfx_res_x() - msg_bounds.x) / 2, .y = (gfx_res_y() - msg_bounds.y) / 2}, COLOR32(255, 255, 255, 255), COLOR32(128, 0, 0, 0), text);
    //Display it
    gfx_flip();
    //Hang
    while(1);
}

/*
 * Checks if certain point is inside a rectangle
 */
uint8_t gfx_point_in_rect(p2d_t p, p2d_t pos, p2d_t sz){
    return p.x >= pos.x &&
           p.y >= pos.y &&
           p.x <= pos.x + sz.x &&
           p.y <= pos.y + sz.y;
}

/*
 * Shift the entire screen up by a number of lines
 */
void gfx_shift_up(uint32_t lines){
    color32_t* buf = gfx_buffer();
    for(uint32_t i = 0; i < lines; i++){
        //From top to bottom
        for(uint32_t l = 1; l < res_x; l++){
            uint32_t offs = l * res_x;
            //Shift one line
            memcpy(&buf[offs - res_x], &buf[offs], res_x * sizeof(color32_t));
        }
    }
}

//Position on screen in verbose logging mode
uint32_t verbose_position = 0;

/*
 * Prints a string while in verbose logging mode
 */
void gfx_verbose_println(char* msg){
    if(!verbose_enabled)
        return;
    //Calculate the bounds of the text
    p2d_t text_bounds = gfx_text_bounds(msg);
    //If it will be printed off screen, shift it up
    if(verbose_position + text_bounds.y >= res_y){
        gfx_shift_up(text_bounds.y);
        verbose_position -= text_bounds.y;
    }
    //Print the string
    gfx_puts((p2d_t){.x = 0, .y = verbose_position}, COLOR32(255, 255, 255, 255), COLOR32(128, 0, 0, 0), msg);
    //Go to the next position
    verbose_position += text_bounds.y;
    //Flip the buffers
    gfx_flip();
}

/*
 * Enable/disable gfx_verbose_println() function
 */
void gfx_set_verbose(uint8_t v){
    verbose_enabled = v;
}