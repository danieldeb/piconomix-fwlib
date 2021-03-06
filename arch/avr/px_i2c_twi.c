/* =============================================================================
     ____    ___    ____    ___    _   _    ___    __  __   ___  __  __ TM
    |  _ \  |_ _|  / ___|  / _ \  | \ | |  / _ \  |  \/  | |_ _| \ \/ /
    | |_) |  | |  | |     | | | | |  \| | | | | | | |\/| |  | |   \  /
    |  __/   | |  | |___  | |_| | | |\  | | |_| | | |  | |  | |   /  \
    |_|     |___|  \____|  \___/  |_| \_|  \___/  |_|  |_| |___| /_/\_\

    Copyright (c) 2006 Pieter Conradie <https://piconomix.com>
 
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

    Title:          px_i2c.h : Interrupt-driven TWI (I2C) Master
    Author(s):      Pieter Conradie
    Creation Date:  2007-03-31

============================================================================= */

/* _____STANDARD INCLUDES____________________________________________________ */
#include <compat/twi.h>
#include <avr/interrupt.h>

/* _____PROJECT INCLUDES_____________________________________________________ */
#include "px_i2c.h"
#include "px_board.h"

#include "px_dbg.h"
PX_DBG_DECL_NAME("px_i2c_twi")

/* _____LOCAL DEFINITIONS____________________________________________________ */
/// Definition of data for each I2C peripheral
typedef struct px_i2c_data_s
{
    /// Peripheral
    px_i2c_per_t peripheral;
    /// Number of open handles referencing peripheral
    uint8_t open_counter;
} px_i2c_data_t;

// Map TWI clock prescaler to prescaler value
#if   (PX_I2C_CFG_TWI_PRESCALER_VAL == 1)
#define PX_I2C_TWI_PRESCALER ((0<<TWPS1)|(0<<TWPS0))
#elif (PX_I2C_CFG_TWI_PRESCALER_VAL == 4)
#define PX_I2C_TWI_PRESCALER ((0<<TWPS1)|(1<<TWPS0))
#elif (PX_I2C_CFG_TWI_PRESCALER_VAL == 16)
#define PX_I2C_TWI_PRESCALER ((1<<TWPS1)|(0<<TWPS0))
#elif (PX_I2C_CFG_TWI_PRESCALER_VAL == 64)
#define PX_I2C_TWI_PRESCALER ((1<<TWPS1)|(1<<TWPS0))
#else
#error "Invalid PX_I2C_CFG_TWI_PRESCALER_VAL value!"
#endif

// Calculate TWI Baud rate value according to selected frequency and prescaler
#define PX_I2C_TWI_BR_VAL \
    ((PX_UDIV_ROUND(F_CPU, PX_I2C_CFG_BIT_RATE_HZ)-16ul) / (2ul * PX_I2C_CFG_TWI_PRESCALER_VAL))

// Make sure that TWI Baud rate value is an 8-bit value
#if (PX_I2C_TWI_BR_VAL > 255)
#error "Make PX_I2C_CFG_TWI_PRESCALER_VAL larger!"
#endif

/// I2C Start transaction with Read (1) or Write (0) bit
typedef enum
{
    PX_I2C_START_W = 0,
    PX_I2C_START_R = 1,    
} px_i2c_start_t;

/* _____MACROS_______________________________________________________________ */

/* _____GLOBAL VARIABLES_____________________________________________________ */

/* _____LOCAL VARIABLES______________________________________________________ */
/// Allocate data for each enabled I2C peripheral
#if PX_I2C_CFG_I2C0_EN
static px_i2c_data_t px_i2c0_data;
#endif

/* _____LOCAL FUNCTION DECLARATIONS__________________________________________ */
static bool px_i2c_start               (uint8_t sla_adr, px_i2c_start_t start);
static bool px_i2c_start_repeat        (uint8_t sla_adr, px_i2c_start_t start);
static bool px_i2c_stop                (void);
static bool px_i2c_wr_u8               (uint8_t data);
static bool px_i2c_rd_u8               (uint8_t *data, bool nak);
static bool px_i2c_init_peripheral     (px_i2c_per_t peripheral);

static void px_i2c_init_peripheral_data(px_i2c_per_t    peripheral,
                                        px_i2c_data_t * i2c_data);

/* _____LOCAL FUNCTIONS______________________________________________________ */
static bool px_i2c_start(uint8_t sla_adr, px_i2c_start_t start)
{
    // Send START condition
    TWCR = (1<<TWINT) | (1<<TWSTA) | (1<<TWEN);
    // Wait for TWINT flag to be set
    PX_WAIT_UNTIL_BIT_IS_HI(TWCR, TWINT);
    // Check TWI Status
    if((TW_STATUS & 0xf8) != TW_START)
    {
        PX_DBG_ERR("TWI_STATUS != TW_START");
        return false;
    }
    // Add R/W bit
    sla_adr = (sla_adr << 1) | start;
    // Send SLA+R/W
    TWDR = sla_adr;
    TWCR = (1<<TWINT)|(1<<TWEN);
    // Wait for TWINT flag to be set
    PX_WAIT_UNTIL_BIT_IS_HI(TWCR, TWINT);
    // Master Receiver?
    if((sla_adr & (1<<0)) != 0)
    {
        // Check TWI Status
        if((TW_STATUS&0xf8) != TW_MR_SLA_ACK)
        {
            PX_DBG_WARN("TWI_STATUS != TW_MR_SLA_ACK");
            // Error
            px_i2c_stop();
            return false;
        }
    }
    else
    {
        // Check TWI Status
        if((TW_STATUS&0xf8) != TW_MT_SLA_ACK)
        {
            PX_DBG_WARN("TWI_STATUS != TW_MT_SLA_ACK");
            // Error
            px_i2c_stop();
            return false;
        }
    }
    
    return true;
}

static bool px_i2c_start_repeat(uint8_t sla_adr, px_i2c_start_t start)
{
    // Send START condition
    TWCR = (1<<TWINT) | (1<<TWSTA) | (1<<TWEN);
    // Wait for TWINT flag to be set
    PX_WAIT_UNTIL_BIT_IS_HI(TWCR, TWINT);
    // Check TWI Status
    if((TW_STATUS & 0xf8) != TW_REP_START)
    {
        PX_DBG_ERR("TWI_STATUS != TW_REP_START");
        return false;
    }
    // Add R/W bit
    sla_adr = (sla_adr << 1) | start;
    // Send SLA+R/W
    TWDR = sla_adr;
    TWCR = (1<<TWINT)|(1<<TWEN);
    // Wait for TWINT flag to be set
    PX_WAIT_UNTIL_BIT_IS_HI(TWCR, TWINT);
    // Master Receiver?
    if((sla_adr & (1<<0)) != 0)
    {
        // Check TWI Status
        if((TW_STATUS & 0xf8) != TW_MR_SLA_ACK)
        {
            PX_DBG_WARN("TWI_STATUS != TW_MR_SLA_ACK");
            // Error
            px_i2c_stop();
            return false;
        }
    }
    else
    {
        // Check TWI Status
        if((TW_STATUS&0xf8) != TW_MT_SLA_ACK)
        {
            PX_DBG_WARN("TWI_STATUS != TW_MT_SLA_ACK");
            // Error
            px_i2c_stop();
            return false;
        }
    }
    return true;
}

static bool px_i2c_stop(void)
{
#if PX_DBG_LEVEL_ERR
    if((TW_STATUS & 0xf8) == TW_BUS_ERROR)
    {
        PX_DBG_ERR("TW_STATUS = TW_BUS_ERROR");
    }
#endif

    // Send STOP condition
    TWCR = (1<<TWINT) | (1<<TWSTO) | (1<<TWEN);

    // Wait for TWSTO flag to be cleared
    PX_WAIT_UNTIL_BIT_IS_LO(TWCR, TWSTO);

    return true;
}

static bool px_i2c_wr_u8(uint8_t data)
{
    // Send data
    TWDR = data;
    TWCR = (1<<TWINT)|(1<<TWEN);
    // Wait for TWINT flag to be set
    PX_WAIT_UNTIL_BIT_IS_HI(TWCR, TWINT);
    // Check TWI Status
    if((TW_STATUS&0xf8) != TW_MT_DATA_ACK)
    {
        PX_DBG_ERR("TWI_STATUS != TW_MT_DATA_ACK");
        // Error
        px_i2c_stop();
        return false;
    }
    return true;    
}

static bool px_i2c_rd_u8(uint8_t *data, bool nak)
{
    // Read data with ACK/NAK
    if(nak)
    {
        TWCR = (0<<TWEA)|(1<<TWINT)|(1<<TWEN);
    }
    else
    {
        TWCR = (1<<TWEA)|(1<<TWINT)|(1<<TWEN);
    }
    // Wait for TWINT flag to be set
    PX_WAIT_UNTIL_BIT_IS_HI(TWCR, TWINT);
    // Check TWI Status
    if(nak)
    {
        if((TW_STATUS&0xf8) != TW_MR_DATA_NACK)
        {
            PX_DBG_ERR("TWI_STATUS != TW_MR_DATA_NACK");
            // Error
            px_i2c_stop();
            return false;
        }
    }
    else
    {
        if((TW_STATUS&0xf8) != TW_MR_DATA_ACK)
        {
            PX_DBG_ERR("TWI_STATUS != TW_MR_DATA_ACK");
            // Error
            px_i2c_stop();
            return false;
        }
    }

    // Store received byte
    *data = TWDR;

    return true;
}

static bool px_i2c_init_peripheral(px_i2c_per_t peripheral)
{
    switch(peripheral)
    {
#if PX_I2C_CFG_I2C0_EN
    case PX_I2C_PER_0:
        // Initialize TWI clock
        TWSR = PX_I2C_TWI_PRESCALER;
        TWBR = PX_I2C_TWI_BR_VAL;
        // Load data register with default content; release SDA
        TWDR = 0xff;
        // Enable TWI peripheral with interrupt disabled
        TWCR =   (0<<TWINT) | (0<<TWEA) | (0<<TWSTA) | (0<<TWSTO) 
               | (0<<TWWC)  | (1<<TWEN) | (0<<TWIE);
        break;
#endif
    default:
        PX_DBG_ERR("Invalid peripheral");
        return false;
    }

    // Success
    return true;
}

static void px_i2c_init_peripheral_data(px_i2c_per_t    peripheral,
                                        px_i2c_data_t * i2c_data)
{
    // Set peripheral
    i2c_data->peripheral = peripheral;
    // Clear reference counter
    i2c_data->open_counter = 0;
}

/* _____GLOBAL FUNCTIONS_____________________________________________________ */
void px_i2c_init(void)
{
    // Initialize peripheral data for each enabled peripheral
#if PX_I2C_CFG_I2C0_EN
    px_i2c_init_peripheral_data(PX_I2C_PER_0, &px_i2c0_data);
#endif
}

bool px_i2c_open(px_i2c_handle_t * handle,
                 px_i2c_per_t      peripheral,
                 uint8_t           slave_adr)
{
    px_i2c_data_t * i2c_data;

    // Verify that pointer to handle is not NULL
    PX_DBG_ASSERT(handle != NULL);

    // Handle not initialised
    handle->i2c_data = NULL;

    // Set pointer to peripheral data
    switch(peripheral)
    {
#if PX_I2C_CFG_I2C0_EN
    case PX_I2C_PER_0:
        i2c_data = &px_i2c0_data;
        break;
#endif
    default:
        PX_DBG_ERR("Invalid peripheral specified");
        return false;
    }

    // Save 7-bit slave address
    handle->slave_adr = slave_adr;

    // Peripheral closed?
    if(i2c_data->open_counter == 0)
    {
        // Initialise peripheral
        if(!px_i2c_init_peripheral(i2c_data->peripheral))
        {
            // Invalid parameter specified
            return false;
        }
    }    

    // Point handle to peripheral
    handle->i2c_data = i2c_data;

    // Success
    i2c_data->open_counter++;
    return true;
}

bool px_i2c_close(px_i2c_handle_t * handle)
{
    px_i2c_data_t * i2c_data;

    // Verify that pointer to handle is not NULL
    PX_DBG_ASSERT(handle != NULL);
    // Set pointer to peripheral
    i2c_data = handle->i2c_data;
    // Check that handle is open
    PX_DBG_ASSERT(i2c_data != NULL);
    PX_DBG_ASSERT(i2c_data->open_counter != 0);

    // Decrement open count
    i2c_data->open_counter--;

    // More open handles referencing peripheral?
    if(i2c_data->open_counter != 0)
    {
        // Close handle
        handle->i2c_data = NULL;
        // Success
        return true;
    }

    switch(i2c_data->peripheral)
    {
#if PX_I2C_CFG_I2C0_EN
    case PX_I2C_PER_0:
        // Disable TWI peripheral
        TWCR = 0x00;
        break;
#endif
    default:
        break;
    }

   // Close handle
   handle->i2c_data = NULL;

    // Success
    return true;
}

bool px_i2c_wr(px_i2c_handle_t * handle,
               const void *      data,
               size_t            nr_of_bytes,
               uint8_t           flags)
{
    px_i2c_data_t * i2c_data;
    const uint8_t * data_u8 = (const uint8_t *)data;

    // Verify that pointer to handle is not NULL
    PX_DBG_ASSERT(handle != NULL);
    // Set pointer to peripheral
    i2c_data = handle->i2c_data;
    // Check that handle is open
    PX_DBG_ASSERT(i2c_data != NULL);
    PX_DBG_ASSERT(i2c_data->open_counter != 0);
    // Check that slave address is 7 bits
    PX_DBG_ASSERT(handle->slave_adr < 0x80);
#if !DBG
    // Supress compiler warning about unused variable
    (void)i2c_data;
#endif

    // Generate START or REPEATED START condition?
    if(  (flags & PX_I2C_FLAG_START) || (flags & PX_I2C_FLAG_REP_START)  )
    {
        if(flags & PX_I2C_FLAG_START)
        {
            if(!px_i2c_start(handle->slave_adr, PX_I2C_START_W))
            {
                return false;
            }
        }
        else
        {
            if(!px_i2c_start_repeat(handle->slave_adr, PX_I2C_START_W))
            {
                return false;
            }
        }
    }

    while(nr_of_bytes != 0)
    {
        // Send data
        if(!px_i2c_wr_u8(*data_u8))
        {
            // Error
            return false;
        }
        // Next byte
        data_u8++;
        nr_of_bytes--;        
    }

    // Generate STOP condition?
    if(flags & PX_I2C_FLAG_STOP)
    {
        if(!px_i2c_stop())
        {
            return false;
        }
    }

    // Success
    return true;
}

bool px_i2c_rd(px_i2c_handle_t * handle,
               void *            data,
               size_t            nr_of_bytes,
               uint8_t           flags)
{
    px_i2c_data_t * i2c_data;
    uint8_t *       data_u8 = (uint8_t *)data;

    // Verify that pointer to handle is not NULL
    PX_DBG_ASSERT(handle != NULL);
    // Set pointer to peripheral
    i2c_data = handle->i2c_data;
    // Check that handle is open
    PX_DBG_ASSERT(i2c_data != NULL);
    PX_DBG_ASSERT(i2c_data->open_counter != 0);
    // Check that slave address is 7 bits
    PX_DBG_ASSERT(handle->slave_adr < 0x80);
#if !DBG
    // Supress compiler warning about unused variable
    (void)i2c_data;
#endif

    // Generate START or REPEATED START condition?
    if(  (flags & PX_I2C_FLAG_START) || (flags & PX_I2C_FLAG_REP_START)  )
    {
        if(flags & PX_I2C_FLAG_START)
        {
            if(!px_i2c_start(handle->slave_adr, PX_I2C_START_R))
            {
                return false;
            }
        }
        else
        {
            if(!px_i2c_start_repeat(handle->slave_adr, PX_I2C_START_R))
            {
                return false;
            }
        }
    }

    while(nr_of_bytes != 0)
    {
        if(  (nr_of_bytes == 1)
           &&((flags & PX_I2C_FLAG_STOP) || (flags & PX_I2C_FLAG_END))  )
        {
            if(!px_i2c_rd_u8(data_u8, true))
            {
                // Error
                return false;
            }
        }
        else
        {
            if(!px_i2c_rd_u8(data_u8, false))
            {
                // Error
                return false;
            }
        }
        
        // Next byte
        data_u8++;
        nr_of_bytes--;
    }

    // Generate STOP condition?
    if(flags & PX_I2C_FLAG_STOP)
    {
        if(!px_i2c_stop())
        {
            return false;
        }
    }

    // Success
    return true;
}

void px_i2c_change_slave_adr(px_i2c_handle_t * handle,
                             uint8_t           slave_adr)
{
    // Verify that pointer to handle is not NULL
    PX_DBG_ASSERT(handle != NULL);
    // Check that slave address is 7 bits
    PX_DBG_ASSERT(slave_adr < 0x80);

    handle->slave_adr = slave_adr;
}
