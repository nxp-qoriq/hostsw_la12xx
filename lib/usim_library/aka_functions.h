/*
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Copyright 2021-2022 NXP
 *
 * This module implements the MILENAGE algorithm set. Within the security
 * architecture of the 3GPP system there are seven security functions;
 * f1, f1*, f2, f3, f4, f5 and f5* used for authentication and key generation.
 * The operation of these functions is to be specified by each operator and as
 * such is not fully standardised. The algorithms implemented here follow the
 * examples 3GPP TS 35.206 3G Security; Specification of the MILENAGE
 * algorithm set: http://www.3gpp.org/ftp/Specs/html-info/35206.htm
 * 3GPP TS 35.206 3G Security; Specification of the MILENAGE algorithm set:
 * An example algorithm set for the 3GPP authentication and key generation
 * functions f1, f1*, f2, f3, f4, f5 and f5*;
 * Document 2: Algorithm specification
 *
 */
#ifndef _AKA_FUNCTIONS_H
#define _AKA_FUNCTIONS_H

typedef unsigned char u8;

/*--------------------------- prototypes --------------------------*/
void f1(u8 k[16], u8 rand[16], u8 sqn[6], u8 amf[2], u8 mac_a[8],
		const u8 op[16]);
void f2345(u8 k[16], u8 rand[16], u8 res[8], u8 ck[16], u8 ik[16], u8 ak[6],
		const u8 op[16]);
void f1star(u8 k[16], u8 rand[16], u8 sqn[6], u8 amf[2],
		u8 mac_s[8], const u8 op[16]);
void f5star(u8 k[16], u8 rand[16],
	 u8 ak[6], const u8 op[16]);
void ComputeOPc(u8 op_c[16], u8 OP[16]);
void RijndaelKeySchedule(u8 key[16]);
void RijndaelEncrypt(u8 input[16], u8 output[16]);
#endif /* _AKA_FUNCTIONS_H */
