/*************************************************************************
	> File Name: crc.h
	> Author: 
	> Mail: 
	> Created Time: 2018年03月30日 星期五 11时16分18秒
 ************************************************************************/

#ifndef __CRC_H
#define __CRC_H

#include "common.h"

/*
    The value of POLY please refer to:
    https://en.wikipedia.org/wiki/Cyclic_redundancy_check
*/

#ifdef __CRC8
#define CRC8_POLY                           0x31            //CRC-8-Dallas/Maxim
#define CRC8_INIT                           0xFF
#endif

#ifdef __CRC16
#define CRC16_POLY                          0x8005          //CRC-16-IBM
#define CRC16_INIT                          0xFFFF
#endif

#ifdef __CRC32
#define CRC32_POLY                          0x04C11DB7      //CRC-32
#define CRC32_INIT                          0xFFFFFFFF
#endif

int CRC_InitTable(void);
void CRC8_InitTable(void);
uint8_t CRC8_CalculateDirectly(void *data, int DataLength);
uint8_t CRC8_calculate(void *data, int DataLength);
void CRC16_InitTable(void);
uint16_t CRC16_CalculateDirectly(void *data, int DataLength);
uint16_t CRC16_calculate(void *data, int DataLength);
void CRC32_InitTable(void);
uint32_t CRC32_CalculateDirectly(void *data, int DataLength);
uint32_t CRC32_calculate(void *data, int DataLength);


#endif
