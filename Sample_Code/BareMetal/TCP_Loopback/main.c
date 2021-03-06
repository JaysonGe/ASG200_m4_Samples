/*
 * (C) 2005-2020 MediaTek Inc. All rights reserved.
 *
 * Copyright Statement:
 *
 * This MT3620 driver software/firmware and related documentation
 * ("MediaTek Software") are protected under relevant copyright laws.
 * The information contained herein is confidential and proprietary to
 * MediaTek Inc. ("MediaTek"). You may only use, reproduce, modify, or
 * distribute (as applicable) MediaTek Software if you have agreed to and been
 * bound by this Statement and the applicable license agreement with MediaTek
 * ("License Agreement") and been granted explicit permission to do so within
 * the License Agreement ("Permitted User"). If you are not a Permitted User,
 * please cease any access or use of MediaTek Software immediately.
 *
 * BY OPENING THIS FILE, RECEIVER HEREBY UNEQUIVOCALLY ACKNOWLEDGES AND AGREES
 * THAT MEDIATEK SOFTWARE RECEIVED FROM MEDIATEK AND/OR ITS REPRESENTATIVES ARE
 * PROVIDED TO RECEIVER ON AN "AS-IS" BASIS ONLY. MEDIATEK EXPRESSLY DISCLAIMS
 * ANY AND ALL WARRANTIES, EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE OR
 * NONINFRINGEMENT. NEITHER DOES MEDIATEK PROVIDE ANY WARRANTY WHATSOEVER WITH
 * RESPECT TO THE SOFTWARE OF ANY THIRD PARTY WHICH MAY BE USED BY,
 * INCORPORATED IN, OR SUPPLIED WITH MEDIATEK SOFTWARE, AND RECEIVER AGREES TO
 * LOOK ONLY TO SUCH THIRD PARTY FOR ANY WARRANTY CLAIM RELATING THERETO.
 * RECEIVER EXPRESSLY ACKNOWLEDGES THAT IT IS RECEIVER'S SOLE RESPONSIBILITY TO
 * OBTAIN FROM ANY THIRD PARTY ALL PROPER LICENSES CONTAINED IN MEDIATEK
 * SOFTWARE. MEDIATEK SHALL ALSO NOT BE RESPONSIBLE FOR ANY MEDIATEK SOFTWARE
 * RELEASES MADE TO RECEIVER'S SPECIFICATION OR TO CONFORM TO A PARTICULAR
 * STANDARD OR OPEN FORUM. RECEIVER'S SOLE AND EXCLUSIVE REMEDY AND MEDIATEK'S
 * ENTIRE AND CUMULATIVE LIABILITY WITH RESPECT TO MEDIATEK SOFTWARE RELEASED
 * HEREUNDER WILL BE ANY SOFTWARE LICENSE FEES OR SERVICE CHARGE PAID BY
 * RECEIVER TO MEDIATEK DURING THE PRECEDING TWELVE (12) MONTHS FOR SUCH
 * MEDIATEK SOFTWARE AT ISSUE.
 */

#include <stddef.h>
#include <stdbool.h>
#include <stdint.h>

#include "printf.h"
#include "mt3620.h"
#include "os_hal_uart.h"
#include "os_hal_gpt.h"
#include "os_hal_gpio.h"
#include "os_hal_spim.h"

#include "ioLibrary_Driver/Ethernet/socket.h"
#include "ioLibrary_Driver/Ethernet/wizchip_conf.h"
#include "ioLibrary_Driver/Ethernet/W5500/w5500.h"
#include "ioLibrary_Driver/Application/loopback/loopback.h"

/******************************************************************************/
/* Configurations */
/******************************************************************************/
/* UART */
static const uint8_t uart_port_num = OS_HAL_UART_PORT0;

uint8_t spi_master_port_num = OS_HAL_SPIM_ISU1;
uint32_t spi_master_speed = 2 * 10 * 1000; /* KHz */

#define SPIM_CLOCK_POLARITY SPI_CPOL_0
#define SPIM_CLOCK_PHASE SPI_CPHA_0
#define SPIM_RX_MLSB SPI_MSB
#define SPIM_TX_MSLB SPI_MSB
#define SPIM_FULL_DUPLEX_MIN_LEN 1
#define SPIM_FULL_DUPLEX_MAX_LEN 16

/****************************************************************************/
/* Global Variables */
/****************************************************************************/
struct mtk_spi_config spi_default_config = {
    .cpol = SPIM_CLOCK_POLARITY,
    .cpha = SPIM_CLOCK_PHASE,
    .rx_mlsb = SPIM_RX_MLSB,
    .tx_mlsb = SPIM_TX_MSLB,
#if 1
    // 20200527 taylor
    // W5500 NCS
    .slave_sel = SPI_SELECT_DEVICE_1,
#else
    // Original
    .slave_sel = SPI_SELECT_DEVICE_0,
#endif
};
#if 0
uint8_t spim_tx_buf[SPIM_FULL_DUPLEX_MAX_LEN];
uint8_t spim_rx_buf[SPIM_FULL_DUPLEX_MAX_LEN];
#endif
static volatile int g_async_done_flag;

// Default Static Network Configuration for TCP Server //
wiz_NetInfo gWIZNETINFO = {{0x00, 0x08, 0xdc, 0xff, 0xfa, 0xfb},
                           {192, 168, 50, 1},
                           {255, 255, 255, 0},
                           {192, 168, 50, 1},
                           {8, 8, 8, 8},
                           NETINFO_STATIC};

#define USE_READ_SYSRAM
#ifdef USE_READ_SYSRAM
uint8_t __attribute__((unused, section(".sysram"))) s1_Buf[2 * 1024];
uint8_t __attribute__((unused, section(".sysram"))) s2_Buf[2 * 1024];
#else
uint8_t s1_Buf[2048];
uint8_t s2_Buf[2048];
#endif

#define _MAIN_DEBUG_

/******************************************************************************/
/* Applicaiton Hooks */
/******************************************************************************/
/* Hook for "printf". */
void _putchar(char character)
{
    mtk_os_hal_uart_put_char(uart_port_num, character);
    if (character == '\n')
        mtk_os_hal_uart_put_char(uart_port_num, '\r');
}

/******************************************************************************/
/* Functions */
/******************************************************************************/

// check w5500 network setting
void InitPrivateNetInfo(void)
{
    uint8_t tmpstr[6];
    uint8_t i = 0;
    ctlwizchip(CW_GET_ID, (void *)tmpstr);

    if (ctlnetwork(CN_SET_NETINFO, (void *)&gWIZNETINFO) < 0)
    {
        printf("ERROR: ctlnetwork SET\r\n");
    }

    memset((void *)&gWIZNETINFO, 0, sizeof(gWIZNETINFO));

    ctlnetwork(CN_GET_NETINFO, (void *)&gWIZNETINFO);

    printf("\r\n=== %s NET CONF ===\r\n", (char *)tmpstr);
    printf("MAC: %02x:%02x:%02x:%02x:%02x:%02x\r\n", gWIZNETINFO.mac[0], gWIZNETINFO.mac[1], gWIZNETINFO.mac[2],
           gWIZNETINFO.mac[3], gWIZNETINFO.mac[4], gWIZNETINFO.mac[5]);
    printf("SIP: %d.%d.%d.%d\r\n", gWIZNETINFO.ip[0], gWIZNETINFO.ip[1], gWIZNETINFO.ip[2], gWIZNETINFO.ip[3]);
    printf("GAR: %d.%d.%d.%d\r\n", gWIZNETINFO.gw[0], gWIZNETINFO.gw[1], gWIZNETINFO.gw[2], gWIZNETINFO.gw[3]);
    printf("SUB: %d.%d.%d.%d\r\n", gWIZNETINFO.sn[0], gWIZNETINFO.sn[1], gWIZNETINFO.sn[2], gWIZNETINFO.sn[3]);
    printf("DNS: %d.%d.%d.%d\r\n", gWIZNETINFO.dns[0], gWIZNETINFO.dns[1], gWIZNETINFO.dns[2], gWIZNETINFO.dns[3]);
    printf("======================\r\n");

    // socket 0-7 closed
    // lawrence
    for (i = 0; i < 8; i++)
    {
        setSn_CR(i, 0x10);
    }
    printf("Socket 0-7 Closed \r\n");
}

_Noreturn void RTCoreMain(void)
{
    u32 i = 0;

    /* Init Vector Table */
    NVIC_SetupVectorTable();

    /* Init UART */
    mtk_os_hal_uart_ctlr_init(uart_port_num);
    //printf("\nUART Inited (port_num=%d)\n", uart_port_num);

    /* Init SPIM */
    mtk_os_hal_spim_ctlr_init(spi_master_port_num);

    printf("-------------------------------------------\r\n");
    printf(" ASG200_DHCP_Server_RTApp_MT3620_BareMetal \r\n");
    printf(" App built on: " __DATE__ " " __TIME__ "\r\n");

    /* Init W5500 network */
    InitPrivateNetInfo();


#ifdef _MAIN_DEBUG_
    printf("s1_Buf = %#x\r\n", s1_Buf);
    printf("s2_Buf = %#x\r\n", s2_Buf);
#endif

    while (1)
    {
        // Open Multi SOCK(1,2) as as TCP Server
        loopback_tcps(1, s1_Buf, 50000);
        loopback_tcps(2, s2_Buf, 50001);
    }
}
