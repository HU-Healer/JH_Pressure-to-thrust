/**
  **********************************2022 CKYF***********************************
  * @file    Vofa_send.c
  * @brief   和Vofa+上位机通讯函数
	* @author  长空御风 - 杨洛
  ******************************************************************************
  * @attention
  *	
  * 可一次向上位机发送2/4/8个float数据。
  *
  **********************************2022 CKYF***********************************
  */
	
#include "Vofa_send.h"
#include "string.h"
#include "usart.h"

Vofa_data_m_2 Vofa_data_2={.tail={0x00,0x00,0x80,0x7f}};
Vofa_data_m_4 Vofa_data_4={.tail={0x00,0x00,0x80,0x7f}};
Vofa_data_m_8 Vofa_data_8={.tail={0x00,0x00,0x80,0x7f}};
Vofa_data_m_16 Vofa_data_16={.tail={0x00,0x00,0x80,0x7f}};

void Vofa_Send_Data2(float data1, float data2)
{
	Vofa_data_2.ch_data[0] = data1;
	Vofa_data_2.ch_data[1] = data2;
	HAL_UART_Transmit_DMA(&huart3, (uint8_t *)&Vofa_data_2, sizeof(Vofa_data_2));
}

void Vofa_Send_Data4(float data1, float data2,float data3, float data4)
{
	Vofa_data_4.ch_data[0] = data1;
	Vofa_data_4.ch_data[1] = data2;
	Vofa_data_4.ch_data[2] = data3;
	Vofa_data_4.ch_data[3] = data4;
	HAL_UART_Transmit_DMA(&huart3, (uint8_t *)&Vofa_data_4, sizeof(Vofa_data_4));   
}

void Vofa_Send_Data8(float data1, float data2,float data3, float data4,float data5, float data6,float data7, float data8)
{
	Vofa_data_8.ch_data[0] = data1;
	Vofa_data_8.ch_data[1] = data2;
	Vofa_data_8.ch_data[2] = data3;
	Vofa_data_8.ch_data[3] = data4;
	Vofa_data_8.ch_data[4] = data5;
	Vofa_data_8.ch_data[5] = data6;
	Vofa_data_8.ch_data[6] = data7;
	Vofa_data_8.ch_data[7] = data8;
	HAL_UART_Transmit_DMA(&huart3, (uint8_t *)&Vofa_data_8, sizeof(Vofa_data_8));   
}

void Vofa_Send_Data16(float data1, float data2,float data3, float data4,float data5, float data6,float data7, float data8,
	float data9, float data10,float data11, float data12,float data13, float data14,float data15, float data16)
{
	Vofa_data_16.ch_data[0] = data1;
	Vofa_data_16.ch_data[1] = data2;
	Vofa_data_16.ch_data[2] = data3;
	Vofa_data_16.ch_data[3] = data4;
	Vofa_data_16.ch_data[4] = data5;
	Vofa_data_16.ch_data[5] = data6;
	Vofa_data_16.ch_data[6] = data7;
	Vofa_data_16.ch_data[7] = data8;
	Vofa_data_16.ch_data[8] = data9;
	Vofa_data_16.ch_data[9] = data10;
	Vofa_data_16.ch_data[10] = data11;
	Vofa_data_16.ch_data[11] = data12;
	Vofa_data_16.ch_data[12] = data13;
	Vofa_data_16.ch_data[13] = data14;
	Vofa_data_16.ch_data[14] = data15;
	Vofa_data_16.ch_data[15] = data16;
	HAL_UART_Transmit_DMA(&huart3, (uint8_t *)&Vofa_data_16, sizeof(Vofa_data_16));   
}
