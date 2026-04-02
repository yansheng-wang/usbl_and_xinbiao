/*
 * adc_process.c
 *
 *  Created on: 2026年3月28日
 *      Author: 16857
 */
#include "adc_process.h"
#include <stdint.h>
#include "hw_types.h"
#include "soc_C6748.h"
#include "uPP.h"

// ================== 1. 缓冲区与标志位 ==================
#define ADC_SAMPLE_POINTS    (1000)
#define UPP_TRANSFER_BYTES   (ADC_SAMPLE_POINTS * 32) // 32000 字节

// 乒乓 Buffer：放入 L2 RAM 并 64 位对齐
#pragma DATA_SECTION(adc_buffer_ping, ".l2_ram_data")
#pragma DATA_ALIGN(adc_buffer_ping, 8)
uint8_t adc_buffer_ping[UPP_TRANSFER_BYTES];

#pragma DATA_SECTION(adc_buffer_pong, ".l2_ram_data")
#pragma DATA_ALIGN(adc_buffer_pong, 8)
uint8_t adc_buffer_pong[UPP_TRANSFER_BYTES];

// 解析后的最终 4 通道数据 (每个点 32 位)
uint32_t final_ch0_data[ADC_SAMPLE_POINTS];
uint32_t final_ch1_data[ADC_SAMPLE_POINTS];
uint32_t final_ch2_data[ADC_SAMPLE_POINTS];
uint32_t final_ch3_data[ADC_SAMPLE_POINTS];

// 乒乓状态标志位
volatile uint8_t current_dma_writing = 0; // 0:写Ping, 1:写Pong
volatile uint8_t flag_ping_ready = 0;
volatile uint8_t flag_pong_ready = 0;

// ================== 2. 极速查表数组 (LUT) ==================
// 强行放入 L1D RAM，实现 CPU 零等待周期极速访问
#pragma DATA_SECTION(lut_ch0, ".l1d_data")
uint8_t lut_ch0[256];
#pragma DATA_SECTION(lut_ch1, ".l1d_data")
uint8_t lut_ch1[256];
#pragma DATA_SECTION(lut_ch2, ".l1d_data")
uint8_t lut_ch2[256];
#pragma DATA_SECTION(lut_ch3, ".l1d_data")
uint8_t lut_ch3[256];

// 初始化查找表（在 main 函数最开始调用一次即可）
void Init_LUT(void)
{
    for (int i = 0; i < 256; i++) {
        // 提取 8位 字节中的对应位（D0~D3对应位0~位3）
        lut_ch0[i] = (i & 0x01) ? 1 : 0;
        lut_ch1[i] = (i & 0x02) ? 1 : 0;
        lut_ch2[i] = (i & 0x04) ? 1 : 0;
        lut_ch3[i] = (i & 0x08) ? 1 : 0;
    }
}
// ================== 3. uPP 中断服务函数 ==================
void uPPIsr(void)
{
    unsigned int status;
    uPPDMAConfig next_dma_cfg;

    // 1. 获取 Channel A 的中断状态
    status = uPPIntStatus(SOC_UPP_0_REGS, uPP_DMA_CHI);

    // 2. 检查是否是窗口传输结束中断 (EOW)
    if(status & uPP_INT_EOW)
    {
        // 立即清除中断标志位
        uPPIntClear(SOC_UPP_0_REGS, uPP_DMA_CHI, uPP_INT_EOW);

        // 准备下一次搬运的基础参数
        next_dma_cfg.LineCount = 1;
        next_dma_cfg.ByteCount = UPP_TRANSFER_BYTES;
        next_dma_cfg.LineOffsetAddress = 0;

        // 乒乓切换逻辑
        if(current_dma_writing == 0)
        {
            // Ping 刚刚写满了！立刻让 DMA 去写 Pong
            next_dma_cfg.WindowAddress = (unsigned int *)adc_buffer_pong;
            uPPDMATransfer(SOC_UPP_0_REGS, uPP_DMA_CHI, &next_dma_cfg);

            flag_ping_ready = 1;      // 告诉 CPU: Ping 准备好了，去解包吧！
            current_dma_writing = 1;  // 更新状态：DMA 正在写 Pong
        }
        else
        {
            // Pong 刚刚写满了！立刻让 DMA 去写 Ping
            next_dma_cfg.WindowAddress = (unsigned int *)adc_buffer_ping;
            uPPDMATransfer(SOC_UPP_0_REGS, uPP_DMA_CHI, &next_dma_cfg);

            flag_pong_ready = 1;      // 告诉 CPU: Pong 准备好了，去解包吧！
            current_dma_writing = 0;  // 更新状态：DMA 正在写 Ping
        }
    }

    // 3. 检查是否有 FIFO 溢出 (防爆监测)
    if(status & uPP_INT_UOR)
    {
        uPPIntClear(SOC_UPP_0_REGS, uPP_DMA_CHI, uPP_INT_UOR);
        // 如果这里触发，说明总线拥堵，可以设个断点或增加全局错误计数器
    }

    // 4. 通知外设中断处理结束，准备迎接下一次
    uPPEndOfInt(SOC_UPP_0_REGS);
}
// ================== 4. 极速查表解包函数 ==================
void Process_ADC_Data(uint8_t *raw_buffer)
{
    uint32_t ch0_val, ch1_val, ch2_val, ch3_val;
    int byte_idx = 0;

    // 遍历 1000 个采样点
    for(int i = 0; i < ADC_SAMPLE_POINTS; i++)
    {
        ch0_val = 0; ch1_val = 0; ch2_val = 0; ch3_val = 0;

        // 【TI DSP 的魔法指令】：告诉编译器内部循环严格执行 32 次
        // 这会让 C6748 编译器将这个循环彻底展开，消除跳转开销，并开启并行指令流水线！
        #pragma MUST_ITERATE(32, 32, 32)
        for(int bit = 0; bit < 32; bit++)
        {
            // 按顺序拿出一个字节
            uint8_t byte_data = raw_buffer[byte_idx++];

            // 移位并查表组合数据：抛弃了 if/else 和位与运算，直接读取 L1D 内存！
            ch0_val = (ch0_val << 1) | lut_ch0[byte_data];
            ch1_val = (ch1_val << 1) | lut_ch1[byte_data];
            ch2_val = (ch2_val << 1) | lut_ch2[byte_data];
            ch3_val = (ch3_val << 1) | lut_ch3[byte_data];
        }

        // 32 个字节拼装完毕，得到当前这一时刻完整的 4 通道 32 位数据！
        final_ch0_data[i] = ch0_val;
        final_ch1_data[i] = ch1_val;
        final_ch2_data[i] = ch2_val;
        final_ch3_data[i] = ch3_val;
    }
}
