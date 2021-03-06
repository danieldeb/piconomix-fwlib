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

    Title:          px_at25s.h : Adesto AT25S Serial Flash Driver
    Author(s):      Pieter Conradie
    Creation Date:  2018-10-12

============================================================================= */

/* _____STANDARD INCLUDES____________________________________________________ */

/* _____PROJECT INCLUDES_____________________________________________________ */
#include "px_at25s.h"
#include "px_dbg.h"
PX_DBG_DECL_NAME("px_at25s")

/* _____LOCAL DEFINITIONS____________________________________________________ */
/// @name Read commands
//@{
#define PX_AT25S_CMD_READ_ARRAY                     0x03
//@}                                                
                                                    
/// @name Program and Erase commands                
//@{                                                
#define PX_AT25S_CMD_BLOCK_ERASE_4KB                0x20
#define PX_AT25S_CMD_BLOCK_ERASE_32KB               0x52
#define PX_AT25S_CMD_BLOCK_ERASE_64KB               0xd8
#define PX_AT25S_CMD_CHIP_ERASE                     0x60
#define PX_AT25S_CMD_BYTE_PAGE_PROG                 0x02
//@}                                                
                                                    
/// @name Protection commands                  
//@{                                                
#define PX_AT25S_CMD_WR_ENABLE                      0x06
#define PX_AT25S_CMD_WR_DISABLE                     0x04
//@}

/// @name Status Register commands                  
//@{                                                
#define PX_AT25S_CMD_RD_STATUS_REG_BYTE_1           0x05
#define PX_AT25S_CMD_RD_STATUS_REG_BYTE_2           0x35
#define PX_AT25S_CMD_WR_STATUS_REG                  0x01
#define PX_AT25S_CMD_WR_EN_VOL_STATUS_REG           0x50
//@}                                                
                                                    
/// @name Miscellaneous commands                    
//@{                                                
#define PX_AT25S_CMD_RD_MAN_AND_DEVICE_ID           0x9f
#define PX_AT25S_CMD_RD_ID                          0x90
#define PX_AT25S_CMD_DEEP_PWR_DN                    0xb9
#define PX_AT25S_CMD_RESUME_FROM_DEEP_PWR_DN        0xab
//@}

/* _____MACROS_______________________________________________________________ */

/* _____GLOBAL VARIABLES_____________________________________________________ */

/* _____LOCAL VARIABLES______________________________________________________ */
static bool              px_at25s_ready_flag;
static px_spi_handle_t * px_at25s_spi_handle;

/* _____LOCAL FUNCTION DECLARATIONS__________________________________________ */

/* _____LOCAL FUNCTIONS______________________________________________________ */
static void px_at25s_tx_adr(uint32_t address)
{
    uint8_t data[3];

    data[0] = PX_U32_MH8(address);
    data[1] = PX_U32_ML8(address);
    data[2] = PX_U32_LO8(address);

    px_spi_wr(px_at25s_spi_handle, data, 3, 0);
}

static void px_at25s_write_enable(void)
{
    uint8_t data[1];

    data[0] = PX_AT25S_CMD_WR_ENABLE;
    px_spi_wr(px_at25s_spi_handle, data, 1, PX_SPI_FLAG_START_AND_STOP);
}

/* _____GLOBAL FUNCTIONS_____________________________________________________ */
void px_at25s_init(px_spi_handle_t * handle)
{
    // Set flag
    px_at25s_ready_flag = true;
    // Save SPI slave device handle
    px_at25s_spi_handle = handle;
}

void px_at25s_power_down(void)
{
    uint8_t data[1];

    data[0] = PX_AT25S_CMD_DEEP_PWR_DN;
    px_spi_wr(px_at25s_spi_handle, data, 1, PX_SPI_FLAG_START_AND_STOP);
}

void px_at25s_resume_from_power_down(void)
{
    uint8_t data[1];

    data[0] = PX_AT25S_CMD_RESUME_FROM_DEEP_PWR_DN;
    px_spi_wr(px_at25s_spi_handle, data, 1, PX_SPI_FLAG_START_AND_STOP);
}

void px_at25s_rd(void *   buffer,
                 uint32_t address,
                 uint16_t nr_of_bytes)
{
    uint8_t data[1];

    // See if specified address is out of bounds
    PX_DBG_ASSERT(address <= PX_AT25S_ADR_MAX);

    // Wait until Serial Flash is not busy
    while(!px_at25s_ready())
    {
        ;
    }

    // Send command
    data[0] = PX_AT25S_CMD_READ_ARRAY;
    px_spi_wr(px_at25s_spi_handle, data, 1, PX_SPI_FLAG_START);
    
    // Send address
    px_at25s_tx_adr(address);
   
    // Read data
    px_spi_rd(px_at25s_spi_handle, buffer, nr_of_bytes, PX_SPI_FLAG_STOP);
}

void px_at25s_rd_page(void * buffer, uint16_t page)
{
    px_at25s_rd(buffer, 
                page * PX_AT25S_PAGE_SIZE, 
                PX_AT25S_PAGE_SIZE);
}

void px_at25s_rd_page_offset(void *   buffer,
                             uint16_t page,
                             uint8_t  start_byte_in_page,
                             uint8_t  nr_of_bytes)
{
    px_at25s_rd(buffer, 
                page * PX_AT25S_PAGE_SIZE + start_byte_in_page, 
                nr_of_bytes);
}

void px_at25s_wr_page(const void * buffer, uint16_t page)
{
    uint8_t data[1];

    // Wait until Serial Flash is not busy
    while(!px_at25s_ready())
    {
        ;
    }

    // Send Write Enable command
    px_at25s_write_enable();

    // Send command
    data[0] = PX_AT25S_CMD_BYTE_PAGE_PROG;
    px_spi_wr(px_at25s_spi_handle, data, 1, PX_SPI_FLAG_START);

    // Send address
    px_at25s_tx_adr(page * PX_AT25S_PAGE_SIZE);

    // Send data to be written
    px_spi_wr(px_at25s_spi_handle, buffer, PX_AT25S_PAGE_SIZE, PX_SPI_FLAG_STOP);

    // Set flag to busy
    px_at25s_ready_flag = false;
}

void px_at25s_wr_page_offset(const void * buffer,
                             uint16_t     page,
                             uint8_t      start_byte_in_page,
                             uint8_t      nr_of_bytes)
{
    uint8_t data[1];

    // Wait until Serial Flash is not busy
    while(!px_at25s_ready())
    {
        ;
    }

    // Send Write Enable command
    px_at25s_write_enable();

    // Send command
    data[0] = PX_AT25S_CMD_BYTE_PAGE_PROG;
    px_spi_wr(px_at25s_spi_handle, data, 1, PX_SPI_FLAG_START);

    // Send address
    px_at25s_tx_adr(page * PX_AT25S_PAGE_SIZE + start_byte_in_page);

    // Send data to be written
    px_spi_wr(px_at25s_spi_handle, buffer, nr_of_bytes, PX_SPI_FLAG_STOP);

    // Set flag to busy
    px_at25s_ready_flag = false;
}

void px_at25s_erase_block(px_at25s_block_t block,
                          uint16_t         block_nr)
{
    uint8_t  data[1];
    uint32_t adr;

    // Wait until Serial Flash is not busy
    while(!px_at25s_ready())
    {
        ;
    }

    // Send Write Enable command
    px_at25s_write_enable();

    // Select command according to specified block size
    switch(block)
    {
    case PX_AT25S_BLOCK_4KB:   
        data[0] = PX_AT25S_CMD_BLOCK_ERASE_4KB;  
        adr     = (uint32_t)block_nr * PX_AT25S_BLOCK_SIZE_4KB;
        break;
    case PX_AT25S_BLOCK_32KB:  
        data[0] = PX_AT25S_CMD_BLOCK_ERASE_32KB; 
        adr     = (uint32_t)block_nr * PX_AT25S_BLOCK_SIZE_32KB;
        break;
    case PX_AT25S_BLOCK_64KB : 
        data[0] = PX_AT25S_CMD_BLOCK_ERASE_64KB; 
        adr     = (uint32_t)block_nr * PX_AT25S_BLOCK_SIZE_64KB;
        break;
    default:
        PX_DBG_ERR("Invalid block size specified");
        return;
    }

    // Send command
    px_spi_wr(px_at25s_spi_handle, data, 1, PX_SPI_FLAG_START);

    // Send address
    px_at25s_tx_adr(adr);

    // Deselect Serial Flash
    px_spi_wr(px_at25s_spi_handle, NULL, 0, PX_SPI_FLAG_STOP);

    // Set flag to busy
    px_at25s_ready_flag = false;
}

void px_at25s_erase_chip(void)
{
    uint8_t data[1];

    // Wait until Serial Flash is not busy
    while(!px_at25s_ready())
    {
        ;
    }

    // Send Write Enable command
    px_at25s_write_enable();

    // Send command
    data[0] = PX_AT25S_CMD_CHIP_ERASE;
    px_spi_wr(px_at25s_spi_handle, data, 1, PX_SPI_FLAG_START_AND_STOP);

    // Set flag to busy
    px_at25s_ready_flag = false;
}

bool px_at25s_ready(void)
{
    uint8_t data;

    // If flag has already been set, then take shortcut
    if(px_at25s_ready_flag)
    {
        return true;
    }

    // Read status
    data = px_at25s_rd_status_reg1();

    // See if Serial Flash is ready
    if(PX_BIT_IS_LO(data, PX_AT25S_STATUS_REG1_BSY))
    {
        // Set flag
        px_at25s_ready_flag = true;
        return true;
    }
    else
    {
        return false;
    }
}

uint8_t px_at25s_rd_status_reg1(void)
{
    uint8_t data[1];

    // Send command
    data[0] = PX_AT25S_CMD_RD_STATUS_REG_BYTE_1;
    px_spi_wr(px_at25s_spi_handle, data, 1, PX_SPI_FLAG_START);

    // Read status
    px_spi_rd(px_at25s_spi_handle, data, 1, PX_SPI_FLAG_STOP);

    return data[0];
}

void px_at25s_rd_man_and_dev_id(uint8_t * buffer)
{
    uint8_t data[1];

    // Send command
    data[0] = PX_AT25S_CMD_RD_MAN_AND_DEVICE_ID;
    px_spi_wr(px_at25s_spi_handle, data, 1, PX_SPI_FLAG_START);

    // Read manufacturer and device ID
    px_spi_rd(px_at25s_spi_handle, buffer, 3, PX_SPI_FLAG_STOP);
}
