/*************************************************************************
	> File Name: crc.c
	> Author: 
	> Mail: 
	> Created Time: 2018年03月30日 星期五 08时35分23秒
 ************************************************************************/

#include "crc.h"

#ifdef __CRC8
static uint8_t  CRC8_table[256];
#endif

#ifdef __CRC16
static uint16_t CRC16_table[256];
#endif

#ifdef __CRC32
static uint32_t CRC32_table[256];
#endif

#ifdef __CRC8
void CRC8_InitTable(void)
{
    int i;
    int j;
    uint8_t data;
    uint8_t crc;

    for(i = 0; i <= 0xFF; i++)    
    {
        crc = 0;
        data = i;
        for(j = 0; j < 8; j++)
        {
            if((data^crc) & 0x80)
            {
                crc = (crc << 1) ^ CRC8_POLY;
            }
            else
            {
                crc <<= 1;
            }

            data <<= 1;
        }

        CRC8_table[i] = crc;
    }
}

uint8_t CRC8_CalculateDirectly(void *data, int DataLength)
{
    int i;
    uint8_t crc;
    uint8_t *pData = (uint8_t *)data;
    
    crc = CRC8_INIT;
    
    while(0 < DataLength--)
    {
        crc ^= *pData++;
        for(i = 0; i < 8; i++)
        {
            if(crc & 0x80)
            {
                crc = (crc << 1) ^ CRC8_POLY;
            }
            else
            {
                crc <<= 1;
            }
        }
    }

    return crc;
}

uint8_t CRC8_calculate(void *data, int DataLength)
{
    uint8_t crc;

    crc = CRC8_INIT;

    while(0 < DataLength--)
    {
        crc = (crc << 8) ^ CRC8_table[crc ^ *pData++];
    }
    
    return crc;
}
#endif

#ifdef __CRC16
void CRC16_InitTable(void)
{
    int i;
    int j;
    uint16_t data;
    uint16_t crc;

    for(i = 0; i <= 0xFF; i++)    
    {
        crc = 0;
        data = i << 8;
        for(j = 0; j < 8; j++)
        {
            if((data^crc) & 0x8000)
            {
                crc = (crc << 1) ^ CRC16_POLY;
            }
            else
            {
                crc <<= 1;
            }

            data <<= 1;
        }

        CRC16_table[i] = crc;
    }
}

uint16_t CRC16_CalculateDirectly(void *data, int DataLength)
{
    int i;
    uint16_t crc;
    uint8_t *pData = (uint8_t *)data;
    
    crc = CRC16_INIT;
    
    while(0 < DataLength--)
    {
        crc ^= (*pData++ << 8);
        for(i = 0; i < 8; i++)
        {
            if(crc & 0x8000)
            {
                crc = (crc << 1) ^ CRC16_POLY;
            }
            else
            {
                crc <<= 1;
            }
        }
    }

    return crc;
}

uint16_t CRC16_calculate(void *data, int DataLength)
{
    uint16_t crc;
    uint8_t *pData = (uint8_t *)data;

    crc = CRC16_INIT;

    while(0 < DataLength--)
    {
        crc = (crc << 8) ^ CRC16_table[((crc >> 8) ^ *pData++) & 0xFF];
    }
    
    return crc;
}
#endif

#ifdef __CRC32
void CRC32_InitTable(void)
{
    int i;
    int j;
    uint32_t data;
    uint32_t crc;

    for(i = 0; i <= 0xFF; i++)
    {
        data = i << 24;
        crc = 0;
        
        for(j = 0; j < 8; j++)
        {
            if((data^crc) & 0x80000000)
            {
                crc = (crc << 1) ^ CRC32_POLY;
            }
            else
            {
                crc <<= 1;
            }

            data <<= 1;
        }

        CRC32_table[i] = crc;
    }
}

uint32_t CRC32_CalculateDirectly(void *data, int DataLength)
{
    int i;
    uint32_t crc;
    uint8_t *pData = (uint8_t *)data;
    
    crc = CRC32_INIT;
    
    while(0 < DataLength--)
    {
        crc ^= (*pData++ << 24);
        for(i = 0; i < 8; i++)
        {
            if(crc & 0x80000000)
            {
                crc = (crc << 1) ^ CRC32_POLY;
            }
            else
            {
                crc <<= 1;
            }
        }
    }

    return crc;
}

uint32_t CRC32_calculate(void *data, int DataLength)
{
    uint32_t crc;
    uint8_t *pData = (uint8_t *)data;

    crc = CRC32_INIT;

    while(0 < DataLength--)
    {
        crc = (crc << 8) ^ CRC32_table[((crc >> 24) ^ *pData++) & 0xFF];
    }
    
    return crc;
}
#endif
