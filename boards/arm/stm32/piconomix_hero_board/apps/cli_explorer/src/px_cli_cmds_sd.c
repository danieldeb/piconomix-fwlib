/* =============================================================================
     ____    ___    ____    ___    _   _    ___    __  __   ___  __  __ TM
    |  _ \  |_ _|  / ___|  / _ \  | \ | |  / _ \  |  \/  | |_ _| \ \/ /
    | |_) |  | |  | |     | | | | |  \| | | | | | | |\/| |  | |   \  /
    |  __/   | |  | |___  | |_| | | |\  | | |_| | | |  | |  | |   /  \
    |_|     |___|  \____|  \___/  |_| \_|  \___/  |_|  |_| |___| /_/\_\

    Copyright (c) 2018 Pieter Conradie <https://piconomix.com>
 
    Permission is hereby granted, free of charge, to any person obtaining a copy
    of this software and associated documentation files (the "Software"), to
    deal in the Software without restriction, including without limitation the
    rights to use, copy, modify, merge, publish, distribute, sublicense, and/or
    sell copies of the Software, and to permit persons to whom the Software is
    furnished to do so, subject to the following conditions:

    The above copyright notice and this permission notice shall be included in
    all copies or substantial portions of the Software.

    THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
    IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
    FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
    AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
    LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
    FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS
    IN THE SOFTWARE.
 
    Title:          px_cli_cmds_sd.h : CLI commands for SD Card
    Author(s):      Pieter Conradie
    Creation Date:  2018-03-11

============================================================================= */

/* _____STANDARD INCLUDES____________________________________________________ */

/* _____PROJECT INCLUDES_____________________________________________________ */
#include "px_cli.h"
#include "px_sd.h"
#include "px_board.h"
#include "main.h"

#include "ff.h"

#include "px_dbg.h"
PX_DBG_DECL_NAME("cli_cmds_sd")

/* _____LOCAL DEFINITIONS____________________________________________________ */
// Make sure buffer is big enough to hold a block of data
#if (MAIN_BUFFER_SIZE < (PX_SD_BLOCK_SIZE-1))
#error "Make MAIN_BUFFER_SIZE larger"
#endif

/* _____MACROS_______________________________________________________________ */

/* _____GLOBAL VARIABLES_____________________________________________________ */

/* _____LOCAL VARIABLES______________________________________________________ */
// Chan Fs Filesystem object
static FATFS chan_fs_fatfs;
// Chan Fs Directory object
static DIR chan_fs_dir;
// Chan Fs File information object
static FILINFO chan_fs_file_info;

/* _____LOCAL FUNCTION DECLARATIONS__________________________________________ */

/* _____LOCAL FUNCTIONS______________________________________________________ */
static const char* px_cli_cmd_fn_sd_init(uint8_t argc, char* argv[])
{
    int           i;
    px_sd_cid_t * cid   = (px_sd_cid_t *)main_buffer;
    px_sd_csd_t * csd   = (px_sd_csd_t *)main_buffer;
    TCHAR *       label = (TCHAR *)main_buffer;
    DWORD         vsn;

    // Card inserted?
    if(!PX_SD_CARD_DETECTED())
    {
        return "Error. SD Card not inserted";
    }
    if(!px_sd_reset())
    {
        return "Error. Could not initialise SD Card";
    }
    if(!px_sd_read_cid(cid))
    {
        return "Error. Could not read Card ID register";
    }

    printf("Manufacturer ID: %02X\n", cid->mid);
    printf("OEM ID: %c%c\n", cid->oid[0], cid->oid[1]);
    printf("Product name: ");
    for(i=0; i<5; i++)
    {
        putchar(cid->pnm[i]);
    }
    putchar('\n');
    printf("Product rev: %u.%u\n", PX_U8_HI4(cid->prv), PX_U8_LO4(cid->prv));
    printf("Serial nr: %08lX\n", cid->psn);
    printf("Manufacture date: %u/%u\n", 
           2000 + PX_U8_CONCAT_U4(cid->year_hi, cid->year_lo),
           cid->month);

    if(!px_sd_read_csd(csd))
    {
        return "Error. Could not read Card Specific Data register";
    }

    printf("CSD Ver: %u\n", csd->csd_structure);
    printf("Data read access time 1: %u\n", csd->taac);
    printf("Data read access time 2: %u\n", csd->nsac);
    printf("Max data transfer rate: %u\n", csd->tran_speed);
    printf("Max read data block length: %u\n", csd->read_bl_len);
    printf("Capacity: %lu Mbyte\n\n",
           (px_sd_get_capacity_in_blocks(csd) * PX_SD_BLOCK_SIZE) / (1024 * 1024));

    // Mount logical drive (immediately)
    if(f_mount(&chan_fs_fatfs, "", 1) != FR_OK)
    {
        return ("Error! Unable to mount drive");
    }

    // Report volume label
    if(f_getlabel("", label, &vsn) == FR_OK)
    {
        printf("Volume '%s' mounted\n", label);
    }

    return NULL;
}

static const char* px_cli_cmd_fn_sd_rd_block(uint8_t argc, char* argv[])
{
    uint32_t block;

    // <block>
    if(!px_cli_util_argv_to_u32(0, 0,PX_U32_MAX))
    {
        printf("Error. <block> must be 0 to %d\n", PX_U32_MAX);
        return NULL;
    }
    block = px_cli_argv_val.u32;

    if(!px_sd_read_block(main_buffer, block))
    {
        return "Error. Unable to read SD block";
    }

    px_cli_util_disp_buf(main_buffer, PX_SD_BLOCK_SIZE);

    return NULL;
}

static const char* px_cli_cmd_fn_sd_ls(uint8_t argc, char* argv[])
{
    const TCHAR * path = "/";
    // [path]
    if(argc >= 1)
    {
        path = argv[0];
    }

    if(f_opendir(&chan_fs_dir, path) != FR_OK)
    {
        return "Error! Unable to open dir";
    }

    while(f_readdir(&chan_fs_dir, &chan_fs_file_info) == FR_OK)
    {
        if(chan_fs_file_info.fname[0] == '\0')
        {
            break;
        }
        printf(chan_fs_file_info.fname);
        putchar('\n');
    }

    if(f_closedir(&chan_fs_dir) != FR_OK)
    {
        return "Error! Unable to close dir";
    }

    return NULL;
}

// Create CLI command structures
PX_CLI_CMD_CREATE(px_cli_cmd_sd_init,      "init", 0, 0,   "",          "Initialize SD Card and report info")
PX_CLI_CMD_CREATE(px_cli_cmd_sd_rd_block,  "r",    1, 1,   "<block>",   "Read SD Block")
PX_CLI_CMD_CREATE(px_cli_cmd_sd_ls,        "ls",   0, 1,   "[path]",    "List files in specified directory")

PX_CLI_GROUP_CREATE(px_cli_group_sd, "sd")
    PX_CLI_CMD_ADD(px_cli_cmd_sd_init,      px_cli_cmd_fn_sd_init)
    PX_CLI_CMD_ADD(px_cli_cmd_sd_rd_block,  px_cli_cmd_fn_sd_rd_block)
    PX_CLI_CMD_ADD(px_cli_cmd_sd_ls,        px_cli_cmd_fn_sd_ls)
PX_CLI_GROUP_END()
