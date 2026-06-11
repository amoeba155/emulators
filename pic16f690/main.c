/*
PIC16F690 Emulator

https://ww1.microchip.com/downloads/aemDocuments/documents/OTH/ProductDocuments/DataSheets/40001262F.pdf

35 instructions

notes:
    sfr_trisa.write_mask
    (*data_memory[1][0x05].mirror).write_mask
    ^- equivalent, allows for literal address read/writes in data_memory

*/

#include <stdint.h>
#include <stdio.h>
#include <stdbool.h>

// shortcuts
#define BITS_13 0b1111111111111;
#define BITS_14 0b11111111111111;

// data memory
#define DATA_MEM_BANKS 4
#define DATA_MEM_ADDRS 0x80

uint16_t demo_1[4] = {
    0b11000000000101,
    0b00000010011001,
    0b00011100011001,
    0b01000110011001,
};

uint16_t program_memory[0x1000] = {0}; // 0xFFF total

typedef struct Register{
    uint8_t value;
    uint8_t write_mask;
    uint8_t read_mask;
    struct Register *mirror;
} Register;

// SFR values ; SFR write rules (unwritable/unimplemented bits)
//--shared
Register sfr_tmr0    = {.write_mask = 0b00000000, .read_mask = 0b11111111, .mirror = &sfr_tmr0};
Register sfr_option  = {.value = 0b11111111, .write_mask = 0b11111111, .read_mask = 0b11111111, .mirror = &sfr_option};
Register sfr_pcl     = {.value = 0b00000000, .write_mask = 0b00000000, .read_mask = 0b11111111, .mirror = &sfr_pcl};
Register sfr_status  = {.value = 0b00011000, .write_mask = 0b00011000, .read_mask = 0b11111111, .mirror = &sfr_status};
Register sfr_fsr     = {.write_mask = 0b00000000, .read_mask = 0b11111111, .mirror = &sfr_fsr};
Register sfr_porta   = {.write_mask = 0b00110111, .read_mask = 0b00111111, .mirror = &sfr_porta};
Register sfr_portb   = {.write_mask = 0b11110000, .read_mask = 0b11110000, .mirror = &sfr_portb};
Register sfr_portc   = {.write_mask = 0b11111111, .read_mask = 0b11111111, .mirror = &sfr_portc};
Register sfr_trisa   = {.value = 0b00000000, .write_mask = 0b00110111, .read_mask = 0b00111111, .mirror = &sfr_trisa};
Register sfr_trisb   = {.value = 0b00000000, .write_mask = 0b11110000, .read_mask = 0b11110000, .mirror = &sfr_trisb};
Register sfr_trisc   = {.value = 0b00000000, .write_mask = 0b11110111, .read_mask = 0b11111111, .mirror = &sfr_trisc};
Register sfr_pclath  = {.value = 0b00000000, .write_mask = 0b00011111, .read_mask = 0b00011111, .mirror = &sfr_pclath};
Register sfr_intcon  = {.value = 0b00000000, .write_mask = 0b11111111, .read_mask = 0b11111111, .mirror = &sfr_intcon};
Register sfr_indf    = {.mirror = &sfr_fsr}; // is first, but cant declare before fsr (for obvious reasons)
//--bank0 exclusive
Register sfr_pir1    = {.value = 0b00000000, .write_mask = 0b01001111, .read_mask = 0b01111111, .mirror = &sfr_pir1};
Register sfr_pir2    = {.value = 0b00000000, .write_mask = 0b11110000, .read_mask = 0b11110000, .mirror = &sfr_pir2};
Register sfr_tmr1l   = {.value = 0b00000000, .write_mask = 0b11111111, .read_mask = 0b11111111, .mirror = &sfr_tmr1l};
Register sfr_tmr1h   = {.value = 0b00000000, .write_mask = 0b11111111, .read_mask = 0b11111111, .mirror = &sfr_tmr1h};
Register sfr_t1con   = {.value = 0b00000000, .write_mask = 0b11111111, .read_mask = 0b11111111, .mirror = &sfr_t1con};
Register sfr_tmr2    = {.value = 0b00000000, .write_mask = 0b11111111, .read_mask = 0b11111111, .mirror = &sfr_tmr2};
Register sfr_t2con   = {.value = 0b00000000, .write_mask = 0b01111111, .read_mask = 0b01111111, .mirror = &sfr_t2con};
Register sfr_sspbuf  = {.value = 0b00000000, .write_mask = 0b11111111, .read_mask = 0b11111111, .mirror = &sfr_sspbuf};
Register sfr_sspcon  = {.value = 0b00000000, .write_mask = 0b11111111, .read_mask = 0b11111111, .mirror = &sfr_sspcon};
Register sfr_ccpr1l  = {.value = 0b00000000, .write_mask = 0b11111111, .read_mask = 0b11111111, .mirror = &sfr_ccpr1l};
Register sfr_ccpr1h  = {.value = 0b00000000, .write_mask = 0b11111111, .read_mask = 0b11111111, .mirror = &sfr_ccpr1h};
Register sfr_ccp1con = {.value = 0b00000000, .write_mask = 0b11111111, .read_mask = 0b11111111, .mirror = &sfr_ccp1con};
Register sfr_rcsta   = {.value = 0b00000000, .write_mask = 0b11111000, .read_mask = 0b11111111, .mirror = &sfr_rcsta};
Register sfr_txreg   = {.value = 0b00000000, .write_mask = 0b11111111, .read_mask = 0b11111111, .mirror = &sfr_txreg};
Register sfr_rcreg   = {.value = 0b00000000, .write_mask = 0b00000000, .read_mask = 0b11111111, .mirror = &sfr_rcreg};
Register sfr_pwm1con = {.value = 0b00000000, .write_mask = 0b11111111, .read_mask = 0b11111111, .mirror = &sfr_pwm1con};
Register sfr_eccpas  = {.value = 0b00000000, .write_mask = 0b11111111, .read_mask = 0b11111111, .mirror = &sfr_eccpas};
Register sfr_adresh  = {.value = 0b00000000, .write_mask = 0b11111111, .read_mask = 0b11111111, .mirror = &sfr_adresh};
Register sfr_adcon0  = {.value = 0b00000000, .write_mask = 0b11111111, .read_mask = 0b11111111, .mirror = &sfr_adcon0};
//--bank1 exclusive
Register sfr_pie1    = {.value = 0b00000000, .write_mask = 0b01111111, .read_mask = 0b01111111, .mirror = &sfr_pie1};
Register sfr_pie2    = {.value = 0b00000000, .write_mask = 0b11110000, .read_mask = 0b11110000, .mirror = &sfr_pie2};
Register sfr_pcon    = {.value = 0b00010000, .write_mask = 0b00110011, .read_mask = 0b00110011, .mirror = &sfr_pcon};
Register sfr_osccon  = {.value = 0b01100000, .write_mask = 0b01110001, .read_mask = 0b01111111, .mirror = &sfr_osccon};
Register sfr_osctune = {.value = 0b00000000, .write_mask = 0b00011111, .read_mask = 0b00011111, .mirror = &sfr_osctune};
Register sfr_pr2     = {.value = 0b11111111, .write_mask = 0b11111111, .read_mask = 0b11111111, .mirror = &sfr_pr2};
Register sfr_sspadd  = {.value = 0b00000000, .write_mask = 0b11111111, .read_mask = 0b11111111, .mirror = &sfr_sspadd};
Register sfr_sspmsk  = {.value = 0b11111111, .write_mask = 0b11111111, .read_mask = 0b11111111, .mirror = &sfr_sspmsk};
Register sfr_sspstat = {.value = 0b00000000, .write_mask = 0b11000000, .read_mask = 0b11111111, .mirror = &sfr_sspstat};
Register sfr_wpua    = {.value = 0b00110111, .write_mask = 0b00110111, .read_mask = 0b00110111, .mirror = &sfr_wpua};
Register sfr_ioca    = {.value = 0b00000000, .write_mask = 0b00111111, .read_mask = 0b00111111, .mirror = &sfr_ioca};
Register sfr_wdtcon  = {.value = 0b00001000, .write_mask = 0b00011111, .read_mask = 0b00011111, .mirror = &sfr_wdtcon};
Register sfr_txsta   = {.value = 0b00000010, .write_mask = 0b11111101, .read_mask = 0b11111111, .mirror = &sfr_txsta};
Register sfr_spbrg   = {.value = 0b00000000, .write_mask = 0b11111111, .read_mask = 0b11111111, .mirror = &sfr_spbrg};
Register sfr_spbrgh  = {.value = 0b00000000, .write_mask = 0b11111111, .read_mask = 0b11111111, .mirror = &sfr_spbrgh};
Register sfr_baudctl = {.value = 0b01000000, .write_mask = 0b00011011, .read_mask = 0b11011011, .mirror = &sfr_baudctl};
Register sfr_adresl  = {.value = 0b00000000, .write_mask = 0b11111111, .read_mask = 0b11111111, .mirror = &sfr_adresl};
Register sfr_adcon1  = {.value = 0b00000000, .write_mask = 0b01110000, .read_mask = 0b01110000, .mirror = &sfr_adcon1};
//--bank2 exlusive
Register sfr_eedat   = {.value = 0b00000000, .write_mask = 0b11111111, .read_mask = 0b11111111, .mirror = &sfr_eedat};
Register sfr_eeadr   = {.value = 0b00000000, .write_mask = 0b11111111, .read_mask = 0b11111111, .mirror = &sfr_eeadr};
Register sfr_eedath  = {.value = 0b00000000, .write_mask = 0b00111111, .read_mask = 0b00111111, .mirror = &sfr_eedath};
Register sfr_eeadrh  = {.value = 0b00000000, .write_mask = 0b00001111, .read_mask = 0b00001111, .mirror = &sfr_eeadrh};
Register sfr_wpub    = {.value = 0b11110000, .write_mask = 0b11110000, .read_mask = 0b11110000, .mirror = &sfr_wpub};
Register sfr_iocb    = {.value = 0b00000000, .write_mask = 0b11110000, .read_mask = 0b11110000, .mirror = &sfr_iocb};
Register sfr_vrcon   = {.value = 0b00000000, .write_mask = 0b11111111, .read_mask = 0b11111111, .mirror = &sfr_vrcon};
Register sfr_cm1con0 = {.value = 0b00000000, .write_mask = 0b10110111, .read_mask = 0b11110111, .mirror = &sfr_cm1con0};
Register sfr_cm2con0 = {.value = 0b00000000, .write_mask = 0b10110111, .read_mask = 0b11110111, .mirror = &sfr_cm2con0};
Register sfr_cm2con1 = {.value = 0b00000010, .write_mask = 0b00000011, .read_mask = 0b11000011, .mirror = &sfr_cm2con1};
Register sfr_ansel   = {.value = 0b11111111, .write_mask = 0b11111111, .read_mask = 0b11111111, .mirror = &sfr_ansel};
Register sfr_anselh  = {.value = 0b00001111, .write_mask = 0b00001111, .read_mask = 0b00001111, .mirror = &sfr_anselh};
//--bank3 exclusive
Register sfr_eecon1  = {.value = 0b00000000, .write_mask = 0b10001100, .read_mask = 0b10001111, .mirror = &sfr_eecon1};
Register sfr_eecon2  = {.value = 0b00000000, .write_mask = 0b00000000, .read_mask = 0b11111111, .mirror = &sfr_eecon2};
Register sfr_pstrcon = {.value = 0b00000001, .write_mask = 0b00011111, .read_mask = 0b00011111, .mirror = &sfr_pstrcon};
Register sfr_srcon   = {.value = 0b00000000, .write_mask = 0b11110000, .read_mask = 0b11111100, .mirror = &sfr_srcon};

// unimplemented locations
Register unimplemented = {.value = 0, .write_mask = 0, .read_mask = 0, .mirror = &unimplemented};

Register data_memory[DATA_MEM_BANKS][DATA_MEM_ADDRS] = {0};

void _init_data_mem() {
    // fill data_memory
    // GPRs treated as their own register, and mirror points to themselves (other than exceptions in banks 1,2,3)
    // SFRs given null register with mirror pointing to variable for literal writes to SFR

    // GPRs between 0x20 and 0x70
    for (int i = 0x20; i < 0x70; i++) {
        data_memory[0][i].mirror = &data_memory[0][i];
        data_memory[1][i].mirror = &data_memory[1][i];
        data_memory[2][i].mirror = &data_memory[2][i];
        data_memory[3][i].mirror = &unimplemented;
    }

    // final GPRs in bank0 and mirrors for other 3 banks to bank 0
    for (int i = 0x70; i < 0x80; i++) {
        data_memory[0][i].mirror = &data_memory[0][i];
        data_memory[1][i].mirror = &data_memory[0][i];
        data_memory[2][i].mirror = &data_memory[0][i];
        data_memory[3][i].mirror = &data_memory[0][i];
    }

    // 0x0 -> 0x1F for each bank
    //--shared
    for (int i = 0; i < 4; i++) {
        data_memory[i][0x00].mirror = &sfr_indf;
        data_memory[i][0x02].mirror = &sfr_pcl;
        data_memory[i][0x03].mirror = &sfr_status;
        data_memory[i][0x04].mirror = &sfr_fsr;
        data_memory[i][0x08].mirror = &unimplemented;
        data_memory[i][0x09].mirror = &unimplemented;
        data_memory[i][0x0A].mirror = &sfr_pclath;
        data_memory[i][0x0B].mirror = &sfr_intcon;

        if (i % 2 == 0) {
            data_memory[i][0x01].mirror = &sfr_tmr0;
            data_memory[i][0x05].mirror = &sfr_porta;
            data_memory[i][0x06].mirror = &sfr_portb;
            data_memory[i][0x07].mirror = &sfr_portc;
        } else {
            data_memory[i][0x01].mirror = &sfr_option;
            data_memory[i][0x05].mirror = &sfr_trisa;
            data_memory[i][0x06].mirror = &sfr_trisb;
            data_memory[i][0x07].mirror = &sfr_trisc;
        }
    }
    //--bank0 exclusive
    data_memory[0][0x0C].mirror = &sfr_pir1;
    data_memory[0][0x0D].mirror = &sfr_pir2;
    data_memory[0][0x0E].mirror = &sfr_tmr1l;
    data_memory[0][0x0F].mirror = &sfr_tmr1h;
    data_memory[0][0x10].mirror = &sfr_t1con;
    data_memory[0][0x11].mirror = &sfr_tmr2;
    data_memory[0][0x12].mirror = &sfr_t2con;
    data_memory[0][0x13].mirror = &sfr_sspbuf;
    data_memory[0][0x14].mirror = &sfr_sspcon;
    data_memory[0][0x15].mirror = &sfr_ccpr1l;
    data_memory[0][0x16].mirror = &sfr_ccpr1h;
    data_memory[0][0x17].mirror = &sfr_ccp1con;
    data_memory[0][0x18].mirror = &sfr_rcsta;
    data_memory[0][0x19].mirror = &sfr_txreg;
    data_memory[0][0x1A].mirror = &sfr_rcreg;
    data_memory[0][0x1B].mirror = &unimplemented;
    data_memory[0][0x1C].mirror = &sfr_pwm1con;
    data_memory[0][0x1D].mirror = &sfr_eccpas;
    data_memory[0][0x1E].mirror = &sfr_adresh;
    data_memory[0][0x1F].mirror = &sfr_adcon0;
    //--bank1 exclusive
    data_memory[1][0x0C].mirror = &sfr_pie1;
    data_memory[1][0x0D].mirror = &sfr_pie2;
    data_memory[1][0x0E].mirror = &sfr_pcon;
    data_memory[1][0x0F].mirror = &sfr_osccon;
    data_memory[1][0x10].mirror = &sfr_osctune;
    data_memory[1][0x11].mirror = &unimplemented;
    data_memory[1][0x12].mirror = &sfr_pr2;
    data_memory[1][0x13].mirror = &sfr_sspadd; // sspmsk under conditions
    data_memory[1][0x14].mirror = &sfr_sspstat;
    data_memory[1][0x15].mirror = &sfr_wpua;
    data_memory[1][0x16].mirror = &sfr_ioca;
    data_memory[1][0x17].mirror = &sfr_wdtcon;
    data_memory[1][0x18].mirror = &sfr_txsta;
    data_memory[1][0x19].mirror = &sfr_spbrg;
    data_memory[1][0x1A].mirror = &sfr_spbrgh;
    data_memory[1][0x1B].mirror = &sfr_baudctl;
    data_memory[1][0x1C].mirror = &unimplemented;
    data_memory[1][0x1D].mirror = &unimplemented;
    data_memory[1][0x1E].mirror = &sfr_adresl;
    data_memory[1][0x1F].mirror = &sfr_adcon1;
    //--bank2 exclusive
    data_memory[2][0x0C].mirror = &sfr_eedat;
    data_memory[2][0x0D].mirror = &sfr_eeadr;
    data_memory[2][0x0E].mirror = &sfr_eedath;
    data_memory[2][0x0F].mirror = &sfr_eeadrh;
    data_memory[2][0x10].mirror = &unimplemented;
    data_memory[2][0x11].mirror = &unimplemented;
    data_memory[2][0x12].mirror = &unimplemented;
    data_memory[2][0x13].mirror = &unimplemented;
    data_memory[2][0x14].mirror = &unimplemented;
    data_memory[2][0x15].mirror = &sfr_wpub;
    data_memory[2][0x16].mirror = &sfr_iocb;
    data_memory[2][0x17].mirror = &unimplemented;
    data_memory[2][0x18].mirror = &sfr_vrcon;
    data_memory[2][0x19].mirror = &sfr_cm1con0;
    data_memory[2][0x1A].mirror = &sfr_cm2con0;
    data_memory[2][0x1B].mirror = &sfr_cm2con1;
    data_memory[2][0x1C].mirror = &unimplemented;
    data_memory[2][0x1D].mirror = &unimplemented;
    data_memory[2][0x1E].mirror = &sfr_ansel;
    data_memory[2][0x1F].mirror = &sfr_anselh;
    //--bank3 exclusive
    data_memory[3][0x0C].mirror = &sfr_eecon1;
    data_memory[3][0x0D].mirror = &sfr_eecon2;
    data_memory[3][0x0E].mirror = &unimplemented;
    data_memory[3][0x0F].mirror = &unimplemented;
    data_memory[3][0x10].mirror = &unimplemented;
    data_memory[3][0x11].mirror = &unimplemented;
    data_memory[3][0x12].mirror = &unimplemented;
    data_memory[3][0x13].mirror = &unimplemented;
    data_memory[3][0x14].mirror = &unimplemented;
    data_memory[3][0x15].mirror = &unimplemented;
    data_memory[3][0x16].mirror = &unimplemented;
    data_memory[3][0x17].mirror = &unimplemented;
    data_memory[3][0x18].mirror = &unimplemented;
    data_memory[3][0x19].mirror = &unimplemented;
    data_memory[3][0x1A].mirror = &unimplemented;
    data_memory[3][0x1B].mirror = &unimplemented;
    data_memory[3][0x1C].mirror = &unimplemented;
    data_memory[3][0x1D].mirror = &sfr_pstrcon;
    data_memory[3][0x1E].mirror = &sfr_srcon;
    data_memory[3][0x1F].mirror = &unimplemented;
}

int write8(uint8_t addr, uint8_t data) {
    return 0;
}

void setup() {
    _init_data_mem();
    
    for (int i = 0; i < 4; i++) {
        program_memory[i + 5] = demo_1[i];
    }
}

int main() {
    setup();
    return 0;
}
