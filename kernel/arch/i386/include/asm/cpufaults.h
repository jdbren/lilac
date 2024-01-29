#ifndef x86_FAULTS_H
#define x86_FAULTS_H

void div0();
void debug();
void nmi();
void brkp();
void ovfl();
void bound();
void invldop();
void dna();
void dblflt();
void cso();
void invldtss();
void segnp();
void ssflt();
void gpflt();
void pgflt();
void res();
void flpexc();
void align();
void mchk();
void simd();

#endif
