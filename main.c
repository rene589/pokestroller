#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <stdbool.h>

// General purpose registers
static uint32_t* ER[8];
static uint16_t* R[8];
static uint16_t* E[8];
static uint8_t* RL[8];
static uint8_t* RH[8];

static uint32_t* SP;
// CCR Condition Code Register
// I UI H U N Z V C
struct Flags{
	bool I; // Interrupt mask bit
	bool UI; // User bit
	bool H; // Half carry flag
	bool U; // User bit
	bool N; // Negative flag
	bool Z; // Zero flag
	bool V; // Overflow flag
	bool C; // Carry flag
};	
static struct Flags flags;

static uint8_t* memory;

void printRegistersState(){
	for(int i=0; i < 8; i++){
		printf("ER%d: [0x%08X], ", i, *ER[i]); 
	}
	printf("\n");
	printf("I: %d, H: %d, N: %d, Z: %d, V: %d, C: %d ", flags.I, flags.H, flags.N, flags.Z, flags.V, flags.C);
	printf("\n\n");

}

void printMemory(uint32_t address, int byteCount){
	address = address & 0x0000ffff; // Keep lower 16 bits only
	for(int i = 0; i < byteCount; i++){ 
		printf("MEMORY - 0x%04x -> %02x\n", address + i, memory[address + i]);
	}
}

// With masking here we're ignoring the 0x00XX0000 part of the address for this emulator, as we have one big memory block that goes up to 0xFFFF
void setMemory8(uint32_t address, uint8_t value){
	address = address & 0x0000ffff; // Keep lower 16 bits only
	memory[address] = value; 
}

void setMemory16(uint32_t address, uint16_t value){
	address = address & 0x0000ffff; // Keep lower 16 bits only
	memory[address] = value >> 8; 
	memory[address + 1] = value & 0xFF; 
}

void setMemory32(uint32_t address, uint32_t value){
	address = address & 0x0000ffff; // Keep lower 16 bits only
	memory[address] = value >> 24; 
	memory[address + 1] = (value >> 16) & 0xFF; 
	memory[address + 2] = (value >> 8) & 0xFF; 
	memory[address + 3] = value & 0xFF; 
}

uint16_t getMemory8(uint32_t address){
	address = address & 0x0000ffff; // Keep lower 16 bits only
	return (uint8_t)(memory[address]);
}

uint16_t getMemory16(uint32_t address){
	address = address & 0x0000ffff; // Keep lower 16 bits only
	return (uint16_t)((memory[address] << 8) | (memory[address + 1]));
}

uint32_t getMemory32(uint32_t address){
	address = address & 0x0000ffff; // Keep lower 16 bits only
	return (uint32_t)((memory[address] << 24) | (memory[address + 1] << 16) | (memory[address + 2] << 8) | memory[address + 3]);
}

void setFlagsADD(uint32_t value1, uint32_t value2, int numberOfBits){
	uint32_t maxValue;
	uint32_t maxValueLo;
	uint32_t negativeFlag;
	uint32_t halfCarryFlag;
	switch(numberOfBits){
		case 8:{
			maxValue = 0xFF;
			maxValueLo = 0xF;
			negativeFlag = 0x80;
			halfCarryFlag = 0x8;

			flags.Z = (uint8_t)(value1 + value2) == 0x0;  
			flags.N = (uint8_t)(value1 + value2) & negativeFlag;  

		}break;
		case 16:{
			maxValue = 0xFFFF;
			maxValueLo = 0xFF;
			negativeFlag = 0x8000;
			halfCarryFlag = 0x100;

			flags.Z = (uint16_t)(value1 + value2) == 0x0;  
			flags.N = (uint16_t)(value1 + value2) & negativeFlag;  

		}break;
		case 32:{
			maxValue = 0xFFFFFFFF;
			maxValueLo = 0xFFFF;
			negativeFlag = 0x80000000;
			halfCarryFlag = 0x10000;

			flags.Z = (uint32_t)(value1 + value2) == 0x0;  
			flags.N = (uint32_t)(value1 + value2) & negativeFlag;  


		}break;
	}

	flags.C = ((uint64_t)value1 + value2 > maxValue) ? 1 : 0; 
	flags.V = ~(value1 ^ value2) & ((value1 + value2) ^ value1) & negativeFlag; // If both operands have the same sign and the results is from a different sign, overflow has occured.
	flags.H = (((value1 & maxValueLo) + (value2 & maxValueLo) & halfCarryFlag) == halfCarryFlag) ? 1 : 0; 
}

void setFlagsMOV(uint32_t value, int numberOfBits){
	flags.V = 0;
	flags.Z = (value == 0x0);
	switch(numberOfBits){
		case 8:{
			flags.N = value & 0x80;
		}break;
		case 16:{
			flags.N = value & 0x8000;
		}break;
		case 32:{
			flags.N = value & 0x80000000;
		}break;
	}
}

int main(){
	// 0x0000 - 0xBFFF - ROM 
	// 0xF020 - 0xF0FF - MMIO
	// 0xF780 - 0xFF7F - RAM 
	// 0xFF80 - 0xFFFF - MMIO
	memory = malloc(64 * 1024);
	memset(memory, 0, 64 * 1024);

	FILE* romFile = fopen("roms/routines.bin","r");
	if(!romFile){
		printf("Can't find rom");
	}

	fseek (romFile , 0 , SEEK_END);
	int romSize = ftell (romFile);
	rewind (romFile);

	fread(memory,1,romSize ,romFile);

	// Init general purpose registers
	for(int i=0; i < 8;i++){
		ER[i] = malloc(4);
		*ER[i] = 0;
		R[i] = (uint16_t*) ER[i];
		E[i] = (uint16_t*) ER[i] + 1;
		RL[i] = (uint8_t*) R[i];
		RH[i] = (uint8_t*) R[i] + 1;
	}
	SP = ER[7];
	flags = (struct Flags){0};
	printRegistersState();

	int pc = 0;
	while(pc != romSize){
		uint16_t* currentInstruction = (uint16_t*)(memory + pc);
		// IMPROVEMENT: maybe just use pointers to the ROM, left this way cause it seems cleaner
		uint16_t ab = (*currentInstruction << 8) | (*currentInstruction >> 8); // 0xbHbL aHaL -> aHaL bHbL

		uint8_t a = ab >> 8;
		uint8_t aH = (a >> 4) & 0xF; 
		uint8_t aL = a & 0xF;

		uint8_t b = ab & 0xFF;
		uint8_t bH = (b >> 4) & 0xF;
		uint8_t bL = b & 0xF;

		uint16_t cd = (*(currentInstruction + 1) << 8) | (*(currentInstruction + 1) >> 8);
		uint8_t c = cd >> 8;
		uint8_t cH = (c >> 4) & 0xF; 
		uint8_t cL = c & 0xF;

		uint8_t d = cd & 0xFF;
		uint8_t dH = (d >> 4) & 0xF;
		uint8_t dL = d & 0xF;

		uint16_t ef = (*(currentInstruction + 2) << 8) | (*(currentInstruction + 2) >> 8);
		uint8_t e = ef >> 8;
		uint8_t eH = (e >> 4) & 0xF; 
		uint8_t eL = e & 0xF;

		uint8_t f = ef & 0xFF;
		uint8_t fH = (f >> 4) & 0xF;
		uint8_t fL = f & 0xF;

		uint32_t cdef = cd << 16 | ef;
		switch(aH){
			case 0x0:{
				switch(aL){
					case 0x0:{
						printf("%04x - NOP\n", pc);
					}break;
					case 0x1:{
						switch(bH){ // NOTE. we're ignoring bL here, might not be necesary
							case 0x0:{ // Lots of MOV.l type instructions and push.l + pop.l
								switch(c){
									case 0x6B:{
										switch(dH){
											case 0x0:{ // MOV.l @aa:16, Rd
												uint32_t address = (ef & 0x0000FFFF) | 0x00FF0000; 
												uint32_t value = getMemory32(address);

												int RdIdx = dL & 0b0111;
												uint32_t* Rd = ER[RdIdx]; 

												setFlagsMOV(value, 32);
												*Rd = value;

												printf("%04x - MOV.l @%x:16, ER%d\n", pc, address, RdIdx); 
												printRegistersState();

											}break;

											case 0x8:{ // MOV.l Rs, @aa:16 
												uint32_t address = (cdef & 0x0000FFFF) | 0x00FF0000; 

												int RsIdx = dL & 0b0111;
												uint32_t* Rs = ER[RsIdx]; 

												uint32_t value = *Rs;
												setFlagsMOV(value, 32);
												setMemory32(address, value);

												printf("%04x - MOV.l ER%d,@%x:16 \n", pc, RsIdx, address); 
												printMemory(address, 4);
												printRegistersState();

											}break;
										}
										pc += 4;


									}break;
									case 0x6D:{ // MOV.l @ERs+, ERd --- MOV.l ERs, @-ERd
										char incOrDec = (dH & 0b1000) ? '-' : '+';

										if (incOrDec == '+'){
											int ERsIdx = dH;
											uint32_t* ERs = ER[ERsIdx]; 
											uint32_t value = getMemory32(*ERs);

											*ERs += 4;

											int ERdIdx = dL;
											uint32_t* ERd = ER[ERdIdx]; 

											setFlagsMOV(value, 32);
											*ERd = value;

											printf("%04x - MOV.l @ER%d+, ER%d\n", pc, ERsIdx, ERdIdx); 

										} else{
											int ERdIdx = dH & 0b0111;
											uint32_t* Erd = ER[ERdIdx]; 

											*Erd -= 4;

											int ERsIdx = dL & 0b0111;
											uint32_t* ERs = ER[ERsIdx]; 

											uint32_t value = *ERs;
											setMemory32(*Erd, value);
											setFlagsMOV(value, 32);

											printf("%04x - MOV.l ER%d, @-ER%d, \n", pc, ERsIdx, ERdIdx); 
											printMemory(*Erd, 4);

										}
										printRegistersState();
										pc +=2;


									} break;
									case 0x6F:{ 
										uint16_t disp = ef;
										bool msbDisp = disp & 0x8000;
										uint32_t signExtendedDisp = msbDisp ? (0xFFFF0000 & disp) : disp;

										if (!(dH & 0b1000)){ // From memory  
											int ersIdx = dH & 0b0111;
											uint32_t* erS = ER[ersIdx]; 

											int erdIdx = dL & 0b0111; 
											uint32_t* erD = ER[erdIdx]; 	

											uint32_t value = getMemory32(*erS + signExtendedDisp); 
											*erD = value;
											setFlagsMOV(value, 32);

											printf("%04x - MOV.l @(%d:16, ER%d), ER%d\n", pc, disp, ersIdx, erdIdx); 

										} else{ // To memory 
											int ersIdx = dL & 0b0111;
											uint32_t* erS = ER[ersIdx]; 

											int erdIdx = dH & 0b0111; 
											uint32_t* erD = ER[erdIdx];

											uint32_t value = *erS;
											setFlagsMOV(value, 32);


											setMemory32(*erD + signExtendedDisp, value);
											printf("%04x - MOV.l ER%d,@(%d:16, ER%d)\n", pc, ersIdx, disp, erdIdx); 
											printMemory(*erD + signExtendedDisp, 4);
										}
										printRegistersState();
										pc+=4;
									} break;
									case 0x69:{ // MOV.L @ERs, ERd
										int ERsIdx = dH;
										uint32_t* ERs = ER[ERsIdx]; 
										uint32_t value = getMemory32(*ERs);

										int ERdIdx = dL;
										uint32_t* Rd = ER[ERdIdx]; 

										setFlagsMOV(value, 32);
										*Rd = value;

										printf("%04x - MOV.l @ER%d, ER%d\n", pc, ERsIdx, ERdIdx ); 
										printRegistersState();

										pc += 2;
									}break;

								}

							}break;
							case 0x4:{
								pc+=2;
								if (bL == 0x0 && cH == 0x6){
									switch(cL){
										case 0x9:
										case 0xB:
										case 0xD:
										case 0xF:{
											uint8_t mostSignificantBit = dH >> 7;
											if (mostSignificantBit == 0x1){
												printf("%04x - STC\n", pc);
											}else{
												printf("%04x - LDC\n", pc);
											}
										}break;
									}
								}
							}break;
							case 0x8:{
								printf("%04x - SLEEP\n", pc);
							}break;
							case 0xC:{
								pc+=2;
								if (bL == 0x0 && cH == 0x5){
									switch(cL){
										case 0x0:
										case 0x2:{
											printf("%04x - MULXS\n", pc);
										}break;
									}
								};
							}break;
							case 0xD:{
								pc+=2;
								if (bL == 0x0 && cH == 0x5){
									switch(cL){ // TODO replace with if, and see if merging it with C & F makes it more readable
										case 0x1:
										case 0x3:{
											printf("%04x - DIVXS\n", pc);
										}break;
									}
								};
							}break;
							case 0xF:{
								pc+=2;
								if (bL == 0x0 && cH == 0x6){
									switch(cL){ // TODO replace with if, and see if merging it with C & F makes it more readable
										case 0x4:{
											printf("%04x - OR\n", pc);
										}break;
										case 0x5:{
											printf("%04x - XOR\n", pc);
										}break;
										case 0x6:{
											printf("%04x - AND\n", pc);
										}break;
									}
								};
							}break;

							default:{
								printf("???\n");
							}break;

						}
					}break;
					case 0x2:{
						printf("%04x - STC\n", pc);
					}break;
					case 0x3:{
						printf("%04x - LDC\n", pc);
					}break;
					case 0x4:{
						printf("%04x - ORC\n", pc);
					}break;
					case 0x5:{
						printf("%04x - XORC\n", pc);
					}break;
					case 0x6:{
						printf("%04x - ANDC\n", pc);
					}break;
					case 0x7:{
						printf("%04x - LDC\n", pc);
					}break;
					case 0x8:{ // ADD.B Rs, Rd
						int RsIdx = bH & 0b0111;
						char loOrHiReg1 = (bH & 0b1000) ? 'l' : 'h';
						int RdIdx = bL & 0b0111;
						char loOrHiReg2 = (bL & 0b1000) ? 'l' : 'h';

						uint8_t* Rs = (loOrHiReg1 == 'l') ? RL[RsIdx] : RH[RsIdx]; 
						uint8_t* Rd = (loOrHiReg2 == 'l') ? RL[RdIdx] : RH[RdIdx]; 

						setFlagsADD(*Rd, *Rs, 8);
						*Rd += *Rs;

						printf("%04x - ADD.b R%d%c,R%d%c\n", pc, RsIdx, loOrHiReg1, RdIdx, loOrHiReg2); 
						printRegistersState();
					}break;
					case 0x9:{ // ADD.W Rs, Rd

						int RsIdx = bH & 0b0111;
						char loOrHiReg1 = (bH & 0b1000) ? 'E' : 'R';
						int RdIdx = bL & 0b0111;
						char loOrHiReg2 = (bL & 0b1000) ? 'E' : 'R';

						uint16_t* Rs = (loOrHiReg1 == 'E') ? E[RsIdx] : R[RsIdx]; 
						uint16_t* Rd = (loOrHiReg2 == 'E') ? E[RdIdx] : R[RdIdx];

						setFlagsADD(*Rd, *Rs, 16);

						*Rd += *Rs;
						printf("%04x - ADD.w %c%d,%c%d\n", pc, loOrHiReg1, RsIdx, loOrHiReg2,  RdIdx); 
						printRegistersState();

					}break;
					case 0xA:{
						switch(bH){
							case 0x0:{
								printf("%04x - INC\n", pc);
							}break;
							case 0x8:
							case 0x9:
							case 0xA:
							case 0xB:
							case 0xC:
							case 0xD:
							case 0xE:
							case 0xF:{ // ADD.l ERs, ERd
								int RsIdx = bH & 0b0111;
								int RdIdx = bL & 0b0111;

								uint32_t* Rs = ER[RsIdx];
								uint32_t* Rd = ER[RdIdx];

								setFlagsADD(*Rd, *Rs, 32);

								*Rd += *Rs;
								printf("%04x - ADD.l ER%d, ER%d\n", pc, RsIdx,  RdIdx); 
								printRegistersState();
							}break;

						}
					}break;
					case 0xB:{
						switch(bH){
							case 0x0:
							case 0x8:
							case 0x9:{
								printf("%04x - ADDS\n", pc);
							}break;
							case 0x5:
							case 0x7:
							case 0xD:
							case 0xF:{
								printf("%04x - INC\n", pc);
							}break;
						}
					}break;
					case 0xC:{ // MOV.B Rs, Rd
						int RsIdx = bH & 0b0111;
						char loOrHiReg1 = (bH & 0b1000) ? 'l' : 'h';
						int RdIdx = bL & 0b0111;
						char loOrHiReg2 = (bL & 0b1000) ? 'l' : 'h';

						uint8_t* Rs = (loOrHiReg1 == 'l') ? RL[RsIdx] : RH[RsIdx]; 
						uint8_t* Rd = (loOrHiReg2 == 'l') ? RL[RdIdx] : RH[RdIdx]; 

						setFlagsMOV(*Rs, 8);
						*Rd = *Rs;

						printf("%04x - MOV.b R%d%c,R%d%c\n", pc, RsIdx, loOrHiReg1, RdIdx, loOrHiReg2); 
						printRegistersState();

					}break;
					case 0xD:{ // MOV.W Rs, Rd

						int RsIdx = bH & 0b0111;
						char loOrHiReg1 = (bH & 0b1000) ? 'E' : 'R';
						int RdIdx = bL & 0b0111;
						char loOrHiReg2 = (bL & 0b1000) ? 'E' : 'R';

						uint16_t* Rs = (loOrHiReg1 == 'E') ? E[RsIdx] : R[RsIdx]; 
						uint16_t* Rd = (loOrHiReg2 == 'E') ? E[RdIdx] : R[RdIdx];

						setFlagsMOV(*Rs, 16);

						*Rd = *Rs;
						printf("%04x - MOV.w %c%d,%c%d\n", pc, loOrHiReg1, RsIdx, loOrHiReg2,  RdIdx); 
						printRegistersState();

					}break;
					case 0xE:{
						printf("%04x - ADDX\n", pc);
					}break;
					case 0xF:{
						switch(bH){
							case 0x0:{
								printf("%04x - DAA\n", pc);
							}break;
							case 0x8:
							case 0x9:
							case 0xA:
							case 0xB:
							case 0xC:
							case 0xD:
							case 0xE:
							case 0xF:{// MOV.l ERs, ERd

								int RsIdx = bH & 0b0111;
								int RdIdx = bL & 0b0111;

								uint32_t* Rs = ER[RsIdx];
								uint32_t* Rd = ER[RdIdx];

								setFlagsMOV(*Rs, 32);

								*Rd = *Rs;
								printf("%04x - MOV.l ER%d, ER%d\n", pc, RsIdx,  RdIdx); 
								printRegistersState();
							}break;
						}
					}break;

				}
			}break;
			case 0x1:{
				switch(aL){
					case 0x0:{
						switch(bH){
							case 0x0:
							case 0x1:
							case 0x3:{
								printf("%04x - SHLL\n", pc);
							}break;
							case 0x8:
							case 0x9:
							case 0xB:{
								printf("%04x - SHAL\n", pc);
							}break;
						}
					}break;
					case 0x1:{
						switch(bH){
							case 0x0:
							case 0x1:
							case 0x3:{
								printf("%04x - SHLR\n", pc);
							}break;
							case 0x8:
							case 0x9:
							case 0xB:{
								printf("%04x - SHAR\n", pc);
							}break;
						}
					}break;
					case 0x2:{
						switch(bH){
							case 0x0:
							case 0x1:
							case 0x3:{
								printf("%04x - ROTXL\n", pc);
							}break;
							case 0x8:
							case 0x9:
							case 0xB:{
								printf("%04x - ROTL\n", pc);
							}break;
						}
					}break;
					case 0x3:{
						switch(bH){
							case 0x0:
							case 0x1:
							case 0x3:{
								printf("%04x - ROTXR\n", pc);
							}break;
							case 0x8:
							case 0x9:
							case 0xB:{
								printf("%04x - ROTR\n", pc);
							}break;
						}
					}break;
					case 0x4:{
						printf("%04x - OR.B\n", pc);
					}break;
					case 0x5:{
						printf("%04x - XOR.B\n", pc);
					}break;
					case 0x6:{
						printf("%04x - AND.B\n", pc);
					}break;
					case 0x7:{
						switch(bH){
							case 0x0:
							case 0x1:
							case 0x3:{
								printf("%04x - NOT\n", pc);
							}break;
							case 0x5:
							case 0x7:{
								printf("%04x - EXTU\n", pc);
							}break;
							case 0x8:
							case 0x9:
							case 0xB:{
								printf("%04x - NEG\n", pc);
							}break;
							case 0xD:
							case 0xF:{
								printf("%04x - EXTS\n", pc);
							}break;

						}
					}break;
					case 0x8:
					case 0x9:{
						printf("%04x - SUB\n", pc);
					}break;
					case 0xA:{
						switch(bH){
							case 0x0:{
								printf("%04x - DEC\n", pc);
							}break;
							case 0x8:
							case 0x9:
							case 0xA:
							case 0xB:
							case 0xC:
							case 0xD:
							case 0xE:
							case 0xF:{
								printf("%04x - SUB\n", pc);
							}break;

						}
					}break;
					case 0xB:{
						switch(bH){
							case 0x0:
							case 0x8:
							case 0x9:{
								printf("%04x - SUBS\n", pc);
							}break;
							case 0x5:
							case 0x7:
							case 0xD:
							case 0xF:{
								printf("%04x - DEC\n", pc);
							}break;
						}
					}break;
					case 0xC:
					case 0xD:{
						printf("%04x - CMP\n", pc);
					}break;
					case 0xE:{
						printf("%04x - SUBX\n", pc);
					}break;
					case 0xF:{
						switch(bH){
							case 0x0:{
								printf("%04x - DAS\n", pc);
							}break;
							case 0x8:
							case 0x9:
							case 0xA:
							case 0xB:
							case 0xC:
							case 0xD:
							case 0xE:
							case 0xF:{
								printf("%04x - CMP\n", pc);
							}break;
						}
					}break;



				}
			}break;

			case 0x2:{ // MOV.B @aa:8, Rd
				uint32_t address = (b & 0x000000FF) | 0x00FFFF00; // Upper 16 bits assumed to be 1
				uint8_t value = getMemory8(address);

				int RdIdx = aL & 0b0111;
				char loOrHiReg = (aL & 0b1000) ? 'l' : 'h';
				uint8_t* Rd = (loOrHiReg == 'l') ? RL[RdIdx] : RH[RdIdx]; 

				setFlagsMOV(value, 8);
				*Rd = value;

				printf("%04x - MOV.b @%x:8, R%d%c\n", pc, address, RdIdx, loOrHiReg); 
				printRegistersState();


			}break;
			case 0x3:{ // MOV.B Rs, @aa:8 
				uint32_t address = (b & 0x000000FF) | 0x00FFFF00; // Upper 16 bits assumed to be 1

				int RsIdx = aL & 0b0111;
				char loOrHiReg = (aL & 0b1000) ? 'l' : 'h';
				uint8_t* Rs = (loOrHiReg == 'l') ? RL[RsIdx] : RH[RsIdx]; 

				uint8_t value = *Rs;
				setFlagsMOV(value, 8);
				setMemory8(address, value);

				printf("%04x - MOV.b R%d%c,@%x:8 \n", pc, RsIdx, loOrHiReg, address); 
				printMemory(address, 1);
				printRegistersState();



			}break;
			case 0x4:{
				switch(aL){
					case 0x0:{
						printf("%04x - BRA\n", pc);
					}break;
					case 0x1:{
						printf("%04x - BRN\n", pc);
					}break;
					case 0x2:{
						printf("%04x - BHI\n", pc);
					}break;
					case 0x3:{
						printf("%04x - BLS\n", pc);
					}break;
					case 0x4:{
						printf("%04x - BCC\n", pc);
					}break;
					case 0x5:{
						printf("%04x - BCS\n", pc);
					}break;
					case 0x6:{
						printf("%04x - BNE\n", pc);
					}break;
					case 0x7:{
						printf("%04x - BEQ\n", pc);
					}break;
					case 0x8:{
						printf("%04x - BVC\n", pc);
					}break;
					case 0x9:{
						printf("%04x - BVS\n", pc);
					}break;
					case 0xA:{
						printf("%04x - BPL\n", pc);
					}break;
					case 0xB:{
						printf("%04x - BMI\n", pc);
					}break;
					case 0xC:{
						printf("%04x - BGE\n", pc);
					}break;
					case 0xD:{
						printf("%04x - BLT\n", pc);
					}break;
					case 0xE:{
						printf("%04x - BGT\n", pc);
					}break;
					case 0xF:{
						printf("%04x - BLE\n", pc);
					}break;


				}
			}break;
			case 0x5:{
				switch(aL){
					case 0x0:
					case 0x2:{
						printf("%04x - MULXU\n", pc);
					}break;
					case 0x1:
					case 0x3:{
						printf("%04x - DIVXU\n", pc);
					}break;
					case 0x4:{ // RTS
						printf("%04x - RTS\n", pc);
						pc = getMemory16(*SP) - 2;
						*SP += 1;
						printRegistersState();
					}break;
					case 0x5:{ // BSR d:8
						int8_t disp = b;

						*SP -= 2;
						setMemory16(*SP, pc + 2);

						printf("%04x - BSR @%d:8\n", pc, disp);
						pc = pc + disp - 2; // Sub 2 cause we're incrementing 2 at the end of the loop

						printMemory(*SP, 2);
						printRegistersState();
					}break;
					case 0xC:{ // BSR d:16
						int16_t disp = cd; 
						*SP -= 2;
						setMemory16(*SP, pc + 4);

						printf("%04x - BSR @%d:16\n", pc, disp);
						pc = pc + disp - 2; // Sub 2 cause we're incrementing 2 at the end of the loop

						printMemory(*SP, 2);
						printRegistersState();
					}break;
					case 0x6:{
						printf("%04x - RTE\n", pc);
					}break;
					case 0x7:{
						printf("%04x - TRAPA\n", pc);
					}break;
					case 0x8:{
						switch(bH){
							case 0x0:{
								printf("%04x - BRA\n", pc);
							}break;
							case 0x1:{
								printf("%04x - BRN\n", pc);
							}break;
							case 0x2:{
								printf("%04x - BHI\n", pc);
							}break;
							case 0x3:{
								printf("%04x - BLS\n", pc);
							}break;
							case 0x4:{
								printf("%04x - BCC\n", pc);
							}break;
							case 0x5:{
								printf("%04x - BCS\n", pc);
							}break;
							case 0x6:{
								printf("%04x - BNE\n", pc);
							}break;
							case 0x7:{
								printf("%04x - BEQ\n", pc);
							}break;
							case 0x8:{
								printf("%04x - BVC\n", pc);
							}break;
							case 0x9:{
								printf("%04x - BVS\n", pc);
							}break;
							case 0xA:{
								printf("%04x - BPL\n", pc);
							}break;
							case 0xB:{
								printf("%04x - BMI\n", pc);
							}break;
							case 0xC:{
								printf("%04x - BGE\n", pc);
							}break;
							case 0xD:{
								printf("%04x - BLT\n", pc);
							}break;
							case 0xE:{
								printf("%04x - BGT\n", pc);
							}break;
							case 0xF:{
								printf("%04x - BLE\n", pc);
							}break;


						}				
					}break;
					case 0x9:{ // JMP @ERn
						int erIdx = bH;
						uint32_t* er = ER[erIdx];
						printf("%04x - JMP @ER%d\n", pc, erIdx);
						pc = *er - 2; // Sub 2 cause we're incrementing 2 at the end of the loop
					}break;
					case 0xA:{ // JMP @aa:24
						uint32_t address = (b << 16) | cd;
						printf("%04x - JMP @0x%04x:24\n", pc, address);
						pc = address - 2; // Sub 2 cause we're incrementing 2 at the end of the loop
					}break;
					case 0xB:{ // JMP @@aa:8 - UNUSED IN THE ROM, left unimplemented.
						printf("%04x - ????\n", pc);
					}break;
					case 0xD:{ // JSR @ERn
						int erIdx = bH;
						uint32_t* er = ER[erIdx];
						
						*SP -= 2;
						setMemory16(*SP, pc + 2);

						printf("%04x - JSR @ER%d\n", pc, erIdx);
						pc = *er - 2; // Sub 2 cause we're incrementing 2 at the end of the loop

						printMemory(*SP, 2);
						printRegistersState();
					}break;
					case 0xE:{ // JSR @aa:16
						uint32_t address = (b << 16) | cd;

						*SP -= 2;
						setMemory16(*SP, pc + 4);

						printf("%04x - JSR @0x%04x:24\n", pc, address);
						pc = address - 2; // Sub 2 cause we're incrementing 2 at the end of the loop

						printMemory(*SP, 2);
						printRegistersState();

					}break;
					case 0xF:{ // JSR @@aa:24 - UNUSED IN THE ROM, left unimplemented.
						printf("%04x - ????\n", pc);
					}break;

				}
			}break;
			case 0x6:{
				switch(aL){
					case 0x0:{
						printf("%04x - BSET\n", pc);
					}break;
					case 0x1:{
						printf("%04x - BNOT\n", pc);
					}break;
					case 0x2:{
						printf("%04x - BCLR\n", pc);
					}break;
					case 0x3:{
						printf("%04x - BTST\n", pc);
					}break;
					case 0x4:{
						printf("%04x - OR\n", pc);
					}break;
					case 0x5:{
						printf("%04x - XOR\n", pc);
					}break;
					case 0x6:{
						printf("%04x - AND\n", pc);
					}break;
					case 0x7:{
						uint8_t mostSignificantBit = bH >> 7;
						if (mostSignificantBit == 0x1){
							printf("%04x - BIST\n", pc);
						}else{
							printf("%04x - BST\n", pc);
						}					
					}break;
					case 0x8:{ // MOV.B @ERs, Rd
						int ERsIdx = bH;
						uint32_t* ERs = ER[ERsIdx]; 
						uint8_t value = getMemory8(*ERs);

						int RdIdx = bL & 0b0111;
						char loOrHiReg = (bL & 0b1000) ? 'l' : 'h';
						uint8_t* Rd = (loOrHiReg == 'l') ? RL[RdIdx] : RH[RdIdx]; 

						setFlagsMOV(value, 8);
						*Rd = value;

						printf("%04x - MOV.b @ER%d, R%d%c\n", pc, ERsIdx, RdIdx, loOrHiReg); 
						printRegistersState();


					}break;
					case 0x9:{ // MOV.W @ERs, Rd
						int ERsIdx = bH;
						uint32_t* ERs = ER[ERsIdx]; 
						uint16_t value = getMemory16(*ERs);

						int RdIdx = bL & 0b111;
						char loOrHiReg = (bL & 0b1000) ? 'E' : 'R';
						uint16_t* Rd = (loOrHiReg == 'E') ? E[RdIdx] : R[RdIdx]; 

						setFlagsMOV(value, 16);
						*Rd = value;

						printf("%04x - MOV.w @ER%d, %c%d\n", pc, ERsIdx, loOrHiReg, RdIdx ); 
						printRegistersState();




					} break;
					case 0xA:{ 
						switch(bH){
							case 0x0:{ // MOV.B @aa:16, Rd
								uint32_t address = (cd & 0x0000FFFF) | 0x00FF0000; // Upper 16 bits assumed to be 1
								uint8_t value = getMemory8(address);

								int RdIdx = bL & 0b0111;
								char loOrHiReg = (bL & 0b1000) ? 'l' : 'h';
								uint8_t* Rd = (loOrHiReg == 'l') ? RL[RdIdx] : RH[RdIdx]; 

								setFlagsMOV(value, 8);
								*Rd = value;

								printf("%04x - MOV.b @%x:16, R%d%c\n", pc, address, RdIdx, loOrHiReg); 
								printRegistersState();

							}break;

							case 0x8:{ // MOV.B Rs, @aa:16 
								uint32_t address = (cd & 0x0000FFFF) | 0x00FF0000; // Upper 16 bits assumed to be 1

								int RsIdx = bL & 0b0111;
								char loOrHiReg = (bL & 0b1000) ? 'l' : 'h';
								uint8_t* Rs = (loOrHiReg == 'l') ? RL[RsIdx] : RH[RsIdx]; 

								uint8_t value = *Rs;
								setFlagsMOV(value, 8);
								setMemory8(address, value);

								printf("%04x - MOV.b R%d%c,@%x:16 \n", pc, RsIdx, loOrHiReg, address); 
								printMemory(address, 1);
								printRegistersState();

							}break;
						}
						pc+=2;

					}break;
					case 0xB:{
						switch(bH){
							case 0x0:{ // MOV.w @aa:16, Rd
								uint32_t address = (cd & 0x0000FFFF) | 0x00FF0000; // Upper 16 bits assumed to be 1
								uint16_t value = getMemory16(address);

								int RdIdx = bL & 0b0111;
								char loOrHiReg = (bL & 0b1000) ? 'R' : 'E';
								uint16_t* Rd = (loOrHiReg == 'R') ? R[RdIdx] : E[RdIdx]; 

								setFlagsMOV(value, 16);
								*Rd = value;

								printf("%04x - MOV.w @%x:16, %c%d\n", pc, address, loOrHiReg, RdIdx); 
								printRegistersState();

							}break;

							case 0x8:{ // MOV.B Rs, @aa:16 
								uint32_t address = (cd & 0x0000FFFF) | 0x00FF0000; // Upper 16 bits assumed to be 1

								int RsIdx = bL & 0b0111;
								char loOrHiReg = (bL & 0b1000) ? 'R' : 'E';
								uint16_t* Rs = (loOrHiReg == 'R') ? R[RsIdx] : E[RsIdx]; 

								uint16_t value = *Rs;
								setFlagsMOV(value, 16);
								setMemory16(address, value);

								printf("%04x - MOV.w %c%d,@%x:16 \n", pc, loOrHiReg, RsIdx, address); 
								printMemory(address, 2);
								printRegistersState();

							}break;
						}
						pc+=2;

					}break;
					case 0xC:{ // MOV.B @ERs+, Rd --- MOV.B Rs, @-ERd
						char incOrDec = (bH & 0b1000) ? '-' : '+';

						if (incOrDec == '+'){
							int ERsIdx = bH;
							uint32_t* ERs = ER[ERsIdx]; 
							uint8_t value = getMemory8(*ERs);

							*ERs += 1;

							int RdIdx = bL & 0b0111;
							char loOrHiReg = (bL & 0b1000) ? 'l' : 'h';
							uint8_t* Rd = (loOrHiReg == 'l') ? RL[RdIdx] : RH[RdIdx]; 

							setFlagsMOV(value, 8);
							*Rd = value;

							printf("%04x - MOV.b @ER%d+, R%d%c\n", pc, ERsIdx, RdIdx, loOrHiReg); 

						} else{
							int ErdIdx = bH & 0b0111;
							uint32_t* Erd = ER[ErdIdx]; 

							*Erd -= 1;

							int RsIdx = bL & 0b0111;
							char loOrHiReg = (bL & 0b1000) ? 'l' : 'h';
							uint8_t* Rs = (loOrHiReg == 'l') ? RL[RsIdx] : RH[RsIdx]; 

							uint8_t value = *Rs;
							setMemory8(*Erd, value);
							setFlagsMOV(value, 8);

							printf("%04x - MOV.b R%d%c, @-ER%d, \n", pc, RsIdx, loOrHiReg, ErdIdx); 
							printMemory(*Erd, 1);

						}
						printRegistersState();



					}break;
					case 0xD:{ // MOV.w @ERs+, Rd --- MOV.w Rs, @-ERd
						char incOrDec = (bH & 0b1000) ? '-' : '+';

						if (incOrDec == '+'){
							int ERsIdx = bH;
							uint32_t* ERs = ER[ERsIdx]; 
							uint16_t value = getMemory16(*ERs);

							*ERs += 2;

							int RdIdx = bL & 0b0111;
							char loOrHiReg = (bL & 0b1000) ? 'r' : 'e';
							uint16_t* Rd = (loOrHiReg == 'r') ? R[RdIdx] : E[RdIdx]; 

							setFlagsMOV(value, 16);
							*Rd = value;

							printf("%04x - MOV.w @ER%d+, R%d%c\n", pc, ERsIdx, RdIdx, loOrHiReg); 

						} else{
							int ErdIdx = bH & 0b0111;
							uint32_t* Erd = ER[ErdIdx]; 

							*Erd -= 2;

							int RsIdx = bL & 0b0111;
							char loOrHiReg = (bL & 0b1000) ? 'r' : 'e';
							uint16_t* Rs = (loOrHiReg == 'r') ? R[RsIdx] : E[RsIdx]; 

							uint16_t value = *Rs;
							setMemory16(*Erd, value);
							setFlagsMOV(value, 16);

							printf("%04x - MOV.w R%d%c, @-ER%d, \n", pc, RsIdx, loOrHiReg, ErdIdx); 
							printMemory(*Erd, 2);

						}
						printRegistersState();



					} break;
					case 0xE:{ // MOV.B @(d:16, ERs), Rd
						int RIdx = bL & 0b0111;
						char loOrHiReg = (bL & 0b1000) ? 'l' : 'h';
						uint8_t* r = (loOrHiReg == 'l') ? RL[RIdx] : RH[RIdx]; 

						uint8_t erIdx = bH & 0b0111; 
						uint32_t* er = ER[erIdx]; 

						uint16_t disp = cd;
						bool msbDisp = disp & 0x8000;
						uint32_t signExtendedDisp = msbDisp ? (0xFFFF0000 & disp) : disp;

						if (!(bH & 0b1000)){ // From memory

						uint8_t value = getMemory8(*er + signExtendedDisp);
							*r = value;
							setFlagsMOV(value, 8);

							printf("%04x - MOV.b @(%d:16, ER%d), R%d%c\n", pc, disp, erIdx, RIdx, loOrHiReg); 

						} else{ // To memory
							// NOTE: asumming the contents of the 8 bit register are copied into the first byte pointed by ERd, though that might not be the case 
							// since memory is accesed in 16 bits ? Will need to test
							uint8_t value = *r;
							setFlagsMOV(value, 8);
							setMemory8(*er + signExtendedDisp, value);
							printf("%04x - MOV.b R%d%c, @(%d:16, ER%d), \n", pc, RIdx, loOrHiReg, disp, erIdx); 
							printMemory(*er + signExtendedDisp, 1);
						}
						printRegistersState();
						pc+=2;


					}break;
					case 0xF:{ // MOV.W @(d:16, ERs), Rd
						int RIdx = bL & 0b0111;
						char loOrHiReg = (bL & 0b1000) ? 'R' : 'E';
						uint16_t* r = (loOrHiReg == 'R') ? R[RIdx] : E[RIdx]; 

						uint8_t erIdx = bH & 0b0111; 
						uint32_t* er = ER[erIdx]; 

						uint16_t disp = cd;
						bool msbDisp = disp & 0x8000;
						uint32_t signExtendedDisp = msbDisp ? (0xFFFF0000 & disp) : disp;

						if (!(bH & 0b1000)){ // From memory
							uint16_t value = getMemory16(*er + signExtendedDisp);
							*r = value;
							setFlagsMOV(value, 16);

							printf("%04x - MOV.w @(%d:16, ER%d), %c%d\n", pc, disp, erIdx, loOrHiReg, RIdx); 

						} else{ // To memory
							uint16_t value = *r;
							setFlagsMOV(value, 16);
							setMemory16(*er + signExtendedDisp, value);
							printf("%04x - MOV.w %c%d, @(%d:16, ER%d), \n", pc, loOrHiReg, RIdx, disp, erIdx); 
							printMemory(*er + signExtendedDisp, 2);
						}
						printRegistersState();
						pc+=2;


					}break;
				}
			}break;
			case 0x7:{
				uint8_t mostSignificantBit = bH >> 7;
				switch(aL){
					case 0x0:{
						printf("%04x - BSET\n", pc);
					}break;
					case 0x1:{
						printf("%04x - BNOT\n", pc);
					}break;
					case 0x2:{
						printf("%04x - BCLR\n", pc);
					}break;
					case 0x3:{
						printf("%04x - BTST\n", pc);
					}break;
					case 0x4:{
						if (mostSignificantBit == 0x1){
							printf("%04x - BIOR\n", pc);
						}else{
							printf("%04x - BOR\n", pc);
						}
					}break;
					case 0x5:{
						if (mostSignificantBit == 0x1){
							printf("%04x - BIXOR\n", pc);
						}else{
							printf("%04x - BXOR\n", pc);
						}
					}break;
					case 0x6:{
						if (mostSignificantBit == 0x1){
							printf("%04x - BIAND\n", pc);
						}else{
							printf("%04x - BAND\n", pc);
						}
					}break;
					case 0x7:{
						if (mostSignificantBit == 0x1){
							printf("%04x - BILD\n", pc);
						}else{
							printf("%04x - BLD\n", pc);
						}					
					}break;
					case 0x8:{
						printf("%04x - MOV\n", pc);
					}break;
					case 0x9:{ 


						switch(bH){ // TODO: see if the next isntructions like CMP will use the same logic and abstaact it
							case 0x0:{ // MOV.w #xx:16, Rd
								int RdIdx = bL & 0b111;
								char loOrHiReg1 = (bL & 0b1000) ? 'E' : 'R';

								uint16_t* Rd = (loOrHiReg1 == 'E') ? E[RdIdx] : R[RdIdx]; 

								setFlagsMOV(cd, 16);
								*Rd = cd;

								printf("%04x - MOV.w 0x%x,%c%d\n", pc, cd, loOrHiReg1,  RdIdx); 
								printRegistersState();
								pc+=2;
							}break;
							case 0x1:{ // ADD.w #xx:16, Rd
								int RdIdx = bL & 0b111;
								char loOrHiReg1 = (bL & 0b1000) ? 'E' : 'R';

								uint16_t* Rd = (loOrHiReg1 == 'E') ? E[RdIdx] : R[RdIdx]; 

								setFlagsADD(*Rd, cd, 16);
								*Rd += cd;

								printf("%04x - ADD.w 0x%x,%c%d\n", pc, cd, loOrHiReg1,  RdIdx); 
								printRegistersState();
								pc+=2;
							}break;
							case 0x2:{
								printf("%04x - CMP\n", pc);
							}break;
							case 0x3:{
								printf("%04x - SUB \n", pc);
							}break;
							case 0x4:{
								printf("%04x - OR\n", pc);
							}break;
							case 0x5:{
								printf("%04x - XOR\n", pc);
							}break;
							case 0x6:{
								printf("%04x - AND\n", pc);
							}break;
						}
					}break;

					case 0xA:{ 
						switch(bH){
							case 0x0:{ // MOV.l #xx:32, ERd
								int RdIdx = bL & 0b111; 
								uint32_t* Rd = ER[RdIdx];

								setFlagsMOV(cdef, 32);

								*Rd = cdef;
								printf("%04x - MOV.l 0x%04x, ER%d\n", pc, cdef,  RdIdx); 
								printRegistersState();
								pc+=4;
							}break;
							case 0x1:{ // ADD.l #xx:32, ERd
								int RdIdx = bL & 0b111; 
								uint32_t* Rd = ER[RdIdx];

								setFlagsADD(*Rd, cdef, 32);

								*Rd += cdef;
								printf("%04x - ADD.l 0x%04x, ER%d\n", pc, cdef,  RdIdx); 
								printRegistersState();
								pc+=4;

							}break;
							case 0x2:{
								printf("%04x - CMP\n", pc);
							}break;
							case 0x3:{
								printf("%04x - SUB \n", pc);
							}break;
							case 0x4:{
								printf("%04x - OR\n", pc);
							}break;
							case 0x5:{
								printf("%04x - XOR\n", pc);
							}break;
							case 0x6:{
								printf("%04x - AND\n", pc);
							}break;
						}
					}break;
					case 0xB:{
						printf("%04x - EEPMOV\n", pc);
					}break;
					case 0xC:
					case 0xE:{
						pc+=2;
						// Here bH is the "register designation field" dont know what that is, so ignorign it for now
						// togetherwith bL it can also be "aa" which is the "absolute address field"
						if (cH == 0x6){
							switch(cL){
								case 0x3:{
									printf("%04x - BTST\n", pc);
								}break;
							}
						}else if (cH == 0x7){
							uint8_t mostSignificantBit = dH >> 7;
							switch(cL){
								case 0x3:{
									printf("%04x - BTST\n", pc);
								}break;
								case 0x4:{
									if (mostSignificantBit == 0x1){
										printf("%04x - BIOR\n", pc);
									}else{
										printf("%04x - BOR\n", pc);
									}
								}break;
								case 0x5:{
									if (mostSignificantBit == 0x1){
										printf("%04x - BXOR\n", pc);
									}else{
										printf("%04x - BIXOR\n", pc);
									}
								}break;
								case 0x6:{
									if (mostSignificantBit == 0x1){
										printf("%04x - BIAND\n", pc);
									}else{
										printf("%04x - BAND\n", pc);
									}
								}break;
								case 0x7:{
									if (mostSignificantBit == 0x1){
										printf("%04x - BILD\n", pc);
									}else{
										printf("%04x - BLD\n", pc);
									}
								}break;
							}


						}
					}break;
					case 0xD:
					case 0xF:{ 
						pc+=2;
						// Here bH is the "register designation field" dont know what that is, so ignorign it for now
						// togetherwith bL it can also be "aa" which is the "absolute address field"
						if (cH == 0x6){
							uint8_t mostSignificantBit = dH >> 7;

							if (cL == 0x7){
								if (mostSignificantBit == 0x1){
									printf("%04x - BIST\n", pc);
								}else{
									printf("%04x - BST\n", pc);
							}						}
						}
						if (cH == 0x6 || cH == 0x7){
							switch(cL){
								case 0:{
									printf("%04x - BSET\n", pc);
								}break;
								case 1:{
									printf("%04x - BNOT\n", pc);
								}break;
								case 2:{
									printf("%04x - BCLR\n", pc);
								}break;

							}
						}
					}break;
				}
			}break;
			case 0x8:{ // ADD.B #xx:8, Rd
				int RdIdx = aL & 0b0111;
				char loOrHiReg = (aL & 0b1000) ? 'l' : 'h';

				uint8_t value = (bH << 4) | bL;
				// To get the actual decimal value well need to call get twosComplement function and the isNegative one, but for now we output as unisgned hex	
				uint8_t* Rd = (loOrHiReg == 'l') ? RL[RdIdx] : RH[RdIdx]; 

				setFlagsADD(*Rd, value, 8);
				*Rd += value;

				printf("%04x - ADD.b 0x%x,R%d%c\n", pc, value, RdIdx, loOrHiReg); //Note: Dmitry's dissasembler sometimes outputs address in decimal (0xdd) not sure why
				printRegistersState();
			}break;
			case 0x9:{
				printf("%04x - ADDX\n", pc);
			}break;

			case 0xA:{
				printf("%04x - CMP\n", pc);
			}break;

			case 0xB:{
				printf("%04x - SUBX\n", pc);
			}break;

			case 0xC:{
				printf("%04x - OR\n", pc);
			}break;

			case 0xD:{
				printf("%04x - XOR\n", pc);
			}break;

			case 0xE:{
				printf("%04x - AND\n", pc);
			}break;

			case 0xF:{ // MOV.B #xx:8, Rd
				int RdIdx = aL & 0b0111;
				char loOrHiReg = (aL & 0b1000) ? 'l' : 'h';

				uint8_t value = (bH << 4) | bL;
				uint8_t* Rd = (loOrHiReg == 'l') ? RL[RdIdx] : RH[RdIdx]; 

				setFlagsMOV(value, 8);
				*Rd = value;

				printf("%04x - MOV.b 0x%x,R%d%c\n", pc, value, RdIdx, loOrHiReg); 
				printRegistersState();
			}break;

			default:{
				printf("???\n");
			} break;
		}
		pc+=2;
	}
	fclose(romFile);
}
