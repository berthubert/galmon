#pragma once
/* lovingly lifted from RTK */

unsigned int getbitu(const unsigned char *buff, int pos, int len);
int getbits(const unsigned char *buff, int pos, int len);
void setbitu(unsigned char *buff, int pos, int len, unsigned int data);
unsigned int rtk_crc24q(const unsigned char *buff, int len);
