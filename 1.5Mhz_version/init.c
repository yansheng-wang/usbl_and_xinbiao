/*
 * init.c
 *
 *  Created on: 2026年3月25日
 *      Author: 16857
 */
#include <stdio.h>
#include <c6x.h>

#include "soc_C6748.h"              // DSP C6748 外设寄存器
#include "psc.h"                    // 电源与睡眠控制宏及设备抽象层函数声明
#include "interrupt.h"             // DSP C6748 中断相关应用程序接口函数声明及系统事件号定义
#include "uartStdio.h"             // 串口标准输入输出终端函数声明
#include "upp.h"                    // 通用并行端口设备抽象层函数声明
#include "init.h"
#include <stdlib.h>
#include "no_os_spi.h"
#include "spi.h"
#include "soc_C6748.h"
#include "hw_types.h"
#include "hw_psc_c6748.h"
#include "hw_spi.h"
#include "no_os_alloc.h"
#include "adc_process.h"
struct ti_spi_extra {
    uint32_t base_addr;     // SPI 模块的基地址，比如 SOC_SPI_1_REGS
    uint32_t channel;       // 通道号
    uint32_t input_clk;     // 输入时钟频率
};
void PSCInit(void)
{
    // 对相应外设模块的使能也可以在 BootLoader 中完成
    // 使能 GPIO 模块
    PSCModuleControl(SOC_PSC_1_REGS, HW_PSC_GPIO, PSC_POWERDOMAIN_ALWAYS_ON, PSC_MDCTL_NEXT_ENABLE);

}
/****************************************************************************/
/*                                                                          */
/*              uPP 初始化                                                 */
/*                                                                          */
/****************************************************************************/
void OmaplFpgauPPSetup(void)
{

    // 外设使能
    PSCModuleControl(SOC_PSC_1_REGS, HW_PSC_UPP, PSC_POWERDOMAIN_ALWAYS_ON, PSC_MDCTL_NEXT_ENABLE);
    // 引脚复用配置   q
    uPPPinMuxSetup(uPP_CHA_8BIT);

    // uPP复位
    uPPReset(SOC_UPP_0_REGS);
    uPPThresholdConfig(SOC_UPP_0_REGS, uPP_DMA_CHI, uPP_Threshold_256Bytes);
    // 数据格式配置
    uPPDataFmtConfig(SOC_UPP_0_REGS, uPP_CHA, uPP_DataPackingFmt_RJZE | uPP_DataPacking_FULL
                            | uPP_InterfaceWidth_8BIT | uPP_DataRate_SINGLE);

//    uPPDataFmtConfig(SOC_UPP_0_REGS, uPP_CHB, uPP_DataPackingFmt_LJZE | uPP_DataPacking_FULL
//                            | uPP_InterfaceWidth_8BIT | uPP_DataRate_SINGLE);

    // 通道配置
    uPPChannelConfig(SOC_UPP_0_REGS, uPP_DDRDEMUX_DISABLE | uPP_SDRTXIL_DISABLE | uPP_CHN_ONE
                            | uPP_ALL_RECEIVE);

    // 引脚配置
    uPPPinConfig(SOC_UPP_0_REGS, uPP_CHA, uPP_PIN_TRIS  | uPP_PIN_START);

//    uPPPinConfig(SOC_UPP_0_REGS, uPP_CHB, uPP_PIN_ENABLE | uPP_PIN_WAIT | uPP_PIN_START);

    // 时钟配置
    // uPPCLK = (CPUCLK / 2) / (2 * (DIV + 1) (DIV = 0, 1, 2, 3 ... 15)
    // 456MHz 主频下支持的时钟 114MHz、57MHz、38MHz、28.5MHz、22.8MHz ......
    uPPClkConfig(SOC_UPP_0_REGS, uPP_CHA, 0, 228000000, uPP_PIN_PHASE_NORMAL);
    // 中断使能
    uPPIntEnable(SOC_UPP_0_REGS, uPP_DMA_CHI, uPP_INT_EOW);
//    uPPIntEnable(SOC_UPP_0_REGS, uPP_DMA_CHQ, uPP_INT_EOW);

    // 中断映射
    IntRegister(C674X_MASK_INT5, uPPIsr);
    IntEventMap(C674X_MASK_INT5, SYS_INT_UPP_INT);
    IntEnable(C674X_MASK_INT5);

    // uPP使能
    uPPEnable(SOC_UPP_0_REGS);
}
/****************************************************************************/
/*                                                                          */
/*              uPP 开始接受数据                                                 */
/*                                                                          */
/****************************************************************************/
// 1. 定义采样点数和每次中断处理的数据量
#define ADC_SAMPLE_POINTS    (1000)
// 每个点占用 32 个字节 (32 个 DCLK)
#define UPP_TRANSFER_BYTES   (ADC_SAMPLE_POINTS * 32)

// 2. 接收缓冲区重写：变成 Ping 和 Pong 两个数组
extern uint8_t adc_buffer_ping[];
extern uint8_t adc_buffer_pong[];

extern volatile uint8_t current_dma_writing;
extern volatile uint8_t flag_ping_ready;
extern volatile uint8_t flag_pong_ready;


// 3. 配置 DMA 传输的函数（发令枪：专开第一枪）
void Start_AD4134_DMA_Transfer(void)
{
    uPPDMAConfig adc_dma_config;

    // A. 第一次启动，肯定是指向 Ping 缓冲区
    adc_dma_config.WindowAddress = (unsigned int *)adc_buffer_ping;

    // B. 对于连续的 ADC 1D 数据流，LineCount 永远设为 1
    adc_dma_config.LineCount = 1;

    // C. 传输的总字节数（32000）
    adc_dma_config.ByteCount = UPP_TRANSFER_BYTES;

    // D. 因为只有 1 行，所以不需要行偏移，设为 0
    adc_dma_config.LineOffsetAddress = 0;

    // 标记状态：告诉系统 DMA 现在正在往 Ping 里写数据
    current_dma_writing = 0;

    // E. 启动通道 A (CHI) 的 DMA 传输
    uPPDMATransfer(SOC_UPP_0_REGS, uPP_DMA_CHI, &adc_dma_config);
}
int32_t ti_spi_init(struct no_os_spi_desc **desc, const struct no_os_spi_init_param *param) {
    // ... (保留你前面的内存分配等代码) ...
    if (!desc || !param || !param->extra) return -1;

        // 2. 为 SPI 描述符分配内存
        struct no_os_spi_desc *descriptor = (struct no_os_spi_desc *)no_os_calloc(1, sizeof(*descriptor));

        // 3. 为 TI 平台的特定参数 (extra) 分配内存
        struct ti_spi_extra *extra = (struct ti_spi_extra *)no_os_calloc(1, sizeof(*extra));
        struct ti_spi_extra *p_extra = (struct ti_spi_extra *)param->extra;

        // 4. 检查内存是否分配成功
        if (!descriptor || !extra) {
            if (descriptor) no_os_free(descriptor);
            if (extra) no_os_free(extra);
            return -1;
        }

        // 5. 将传入的硬件参数复制到刚分配好的 extra 结构体中
        extra->base_addr = p_extra->base_addr;
        extra->channel = p_extra->channel;
        extra->input_clk = p_extra->input_clk;

        // 6. 将 extra 绑定到 descriptor 上
        descriptor->extra = extra;

        // 7. 将分配好的 descriptor 传回给外部的 desc 指针
        *desc = descriptor;
    unsigned char dcs = 0x01; // 0x01 表示空闲时 CS0 保持高电平

    // 如果你代码里没有定义 SIMO_SOMI_CLK_CS，请使用这个绝对安全的值：
    // 它代表同时接管 CLK, SIMO, SOMI, CS0 这 4 个引脚
    unsigned int val = 0x00000E01;

    //  [补充步骤 1]：开启 SPI1 模块电源。没有这一步，下面的函数调了也白调
    PSCModuleControl(SOC_PSC_1_REGS, HW_PSC_SPI1, PSC_POWERDOMAIN_ALWAYS_ON, PSC_MDCTL_NEXT_ENABLE);

    /* --- 以下完全采用你提供的官方流程 --- */

    /* Resets the SPI */
    SPIReset(SOC_SPI_1_REGS);

    /* Brings SPI Out-of-Reset */
    SPIOutOfReset(SOC_SPI_1_REGS);

    /* Configures SPI in Master Mode */
    SPIModeConfigure(SOC_SPI_1_REGS, SPI_MASTER_MODE);

    /* Sets SPI Controller for 4-pin Mode with Chip Select */
    SPIPinControl(SOC_SPI_1_REGS, 0, 0, &val);

    /* Configures the Prescale bit in Data Format register. */
    // 这里将 SPI 时钟设为 1MHz (可以根据 AD7768 的需求自行改大)
    SPIClkConfigure(SOC_SPI_1_REGS, 228000000, 1000000, SPI_DATA_FORMAT0);

    /* Chip Select Default Pattern is Set To 1 in SPIDEF Register*/
    SPIDefaultCSSet(SOC_SPI_1_REGS, dcs);

    /* Configures SPI Data Format Register */
    // 注意：标准库里的这个函数其实带好几个参数，如果你的编译器没报错就用你原来的。
    // 如果报错参数太少，请换成这句：(8位数据, 极性高, 相位延迟 -> 对应 AD7768 Mode 3)
    SPIConfigClkFormat(SOC_SPI_1_REGS,
                      (SPI_CLK_POL_LOW | SPI_CLK_INPHASE),
                      SPI_DATA_FORMAT0);

    /* Configures SPI to transmit MSB bit First during data transfer */
    SPIShiftMsbFirst(SOC_SPI_1_REGS, SPI_DATA_FORMAT0);

    /* Sets the Charcter length */
    SPICharLengthSet(SOC_SPI_1_REGS, 8, SPI_DATA_FORMAT0);
    /*  [补充步骤 2]：启动 SPI 状态机！(极其重要，原代码漏了这一步) */
    SPIEnable(SOC_SPI_1_REGS);

    return 0;
}
int32_t ti_spi_write_and_read(struct no_os_spi_desc *desc, uint8_t *data, uint16_t len) {
    uint32_t i;

    // 1. 发送前清理（对应示例的初始化状态）
    while (SPIIntStatus(SOC_SPI_1_REGS, SPI_RECV_INT)) {
        SPIDataReceive(SOC_SPI_1_REGS);
    }

    for (i = 0; i < len; i++) {
        // 等待发送缓冲区就绪
        while (SPIIntStatus(SOC_SPI_1_REGS, SPI_TRANSMIT_INT) == 0);

        // 构建硬件发送指令
        // 0x00FE0000: 选中 CS0, 使用 Format 0
        unsigned int spidat1_val = 0x00FE0000 | data[i];

        // --- 核心逻辑：仿照示例的 SET_SYNC/CLR_SYNC ---
        // 如果不是最后一个字节，我们要保持 CS 为低 (CSHOLD = 1)
        if (i < (len - 1)) {
            spidat1_val |= 0x10000000; // CSHOLD = 1
        } else {
            // 最后一个字节不带 CSHOLD，硬件发完会自动拉高 CS (对应示例末尾的 SET_SYNC)
            spidat1_val &= ~0x10000000;
        }

        // 写入寄存器，波形瞬间喷涌而出
        HWREG(SOC_SPI_1_REGS + 0x3C) = spidat1_val;

        // 等待接收完成（对应示例里的采样 iTemp）
        while (SPIIntStatus(SOC_SPI_1_REGS, SPI_RECV_INT) == 0);

        // 读回数据
        data[i] = (uint8_t)(SPIDataReceive(SOC_SPI_1_REGS) & 0xFF);
    }
    return 0;
}
// 定义一个空的 remove 函数
int32_t ti_spi_remove(struct no_os_spi_desc *desc)
{
    return 0; // 什么都不做，直接返回
}

const struct no_os_spi_platform_ops ti_spi_ops = {
    .init = &ti_spi_init,
    .write_and_read = &ti_spi_write_and_read,
    .remove = &ti_spi_remove // 别给 NULL，给它这个空函数
};






