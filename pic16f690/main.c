/*
PIC16F690 Emulator

https://ww1.microchip.com/downloads/aemDocuments/documents/OTH/ProductDocuments/DataSheets/40001262F.pdf

35 instructions

search "todo:"

If the STATUS
register is the destination for an instruction that affects
the Z, DC or C bits, then the write to these three bits is
disabled.

notes:
    sfr_trisa.write_mask
    (*data_memory[1][0x05].mirror).write_mask
    ^- equivalent, allows for literal address read/writes in data_memory

    pc stack is linked list, top is referenced by sktptr

*/

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

// shortcuts
#define BIT_1   0b1
#define BIT_2   0b10
#define BIT_3   0b100
#define BIT_4   0b1000
#define BIT_5   0b10000
#define BIT_8   0b10000000
#define BIT_9   0b100000000

#define BITS_4  0b1111
#define BITS_7  0b1111111
#define BITS_8  0b11111111
#define BITS_11 0b11111111111
#define BITS_13 0b1111111111111
#define BITS_14 0b11111111111111

// data memory
#define DATA_MEM_BANKS 4
#define DATA_MEM_ADDRS 0x80

// instructions
#define INSTRUCTIONS_COUNT 35

// external flags
uint8_t flag_two_cycle = 0b00; // two bits

uint16_t demo_1[4] = {
0b11000011000011,
0b00000010100001,
0b00110010100001,
0b00110110100001,
};

//todo: mirror program_memory
uint16_t program_memory[0x1000] = {0}; // 0xFFF total
uint16_t program_counter = 0x20;
uint16_t instruction_register = 0;


typedef struct Stack {
    uint16_t value;
    struct Stack *next;
} Stack;


Stack *stkptr = NULL;


void push_pc_stack(Stack **head, uint16_t value) {
    Stack *node = malloc(sizeof(Stack));

    node->value = value;
    node->next = *head;

    *head = node;

    // remove 9th node from stack (if exists)
    node = *head;
    int count = 0;
    while (node != NULL) {
        if (count == 7) {
            Stack *curr = node->next;
            node->next = NULL;

            while (curr != NULL) {
                Stack *next = curr->next;
                free(curr);
                curr = next;
            }

            break;
        }

        node = node->next;
        count++;
    }
}


uint16_t pop_pc_stack(Stack **head) {
    if (*head == NULL) {
        // underflow
        return 0;
    }

    Stack *node = *head;
    uint16_t pc_value = node->value;

    *head = node->next;

    free(node);
    return pc_value;
}


// register struct for EVERYTHING (w, ram, eeprom)
typedef struct Register{
    uint8_t value;
    uint8_t write_mask;
    uint8_t read_mask;
    struct Register *mirror;
} Register;


Register w = {.value = 0b00000000, .write_mask = 0b11111111, .read_mask = 0b11111111, .mirror = &w};

// SFR values ; SFR write rules (unwritable/unimplemented bits)
//--shared
Register sfr_tmr0    = {.write_mask = 0b00000000, .read_mask = 0b11111111, .mirror = &sfr_tmr0};
Register sfr_option  = {.value = 0b11111111, .write_mask = 0b11111111, .read_mask = 0b11111111, .mirror = &sfr_option};
Register sfr_pcl     = {.value = 0b00000000, .write_mask = 0b11111111, .read_mask = 0b11111111, .mirror = &sfr_pcl}; //todo: exception when reading/writing to mirror = &program_counter
Register sfr_status  = {.value = 0b00011000, .write_mask = 0b00011000, .read_mask = 0b11111111, .mirror = &sfr_status};
Register sfr_fsr     = {.write_mask = 0b00000000, .read_mask = 0b11111111, .mirror = &sfr_fsr};
Register sfr_porta   = {.write_mask = 0b00110111, .read_mask = 0b00111111, .mirror = &sfr_porta};
Register sfr_portb   = {.write_mask = 0b11110000, .read_mask = 0b11110000, .mirror = &sfr_portb};
Register sfr_portc   = {.write_mask = 0b11111111, .read_mask = 0b11111111, .mirror = &sfr_portc};
Register sfr_trisa   = {.value = 0b00000000, .write_mask = 0b00110111, .read_mask = 0b00111111, .mirror = &sfr_trisa};
Register sfr_trisb   = {.value = 0b00000000, .write_mask = 0b11110000, .read_mask = 0b11110000, .mirror = &sfr_trisb};
Register sfr_trisc   = {.value = 0b00000000, .write_mask = 0b11110111, .read_mask = 0b11111111, .mirror = &sfr_trisc};
Register sfr_pclath  = {.value = 0b00000000, .write_mask = 0b00011111, .read_mask = 0b00011111, .mirror = &sfr_pclath}; // see above
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

Register data_memory[DATA_MEM_BANKS][DATA_MEM_ADDRS];


// instruction handler
typedef void (*InstructionHandler) (
    Register* w,
    uint16_t opcode
);


void instr_addwf(Register* w, uint16_t opcode) {
    uint8_t file_address = opcode & BITS_7;
    struct Register *file = &data_memory[(sfr_status.value & 0b01100000) >> 5][file_address];
    while (file->mirror != file) {
        file = file->mirror;
    }

    uint16_t result = (file->value & file->read_mask) + w->value;

    // flags
    // c
    if (result & BIT_9) {
        sfr_status.value = sfr_status.value | BIT_1;
    }
    // dc
    if ((((w->value & BITS_4) + (file->value & BITS_4)) & BIT_5) == BIT_5) {
        sfr_status.value = sfr_status.value | BIT_2;
    }
    // z
    if ((result & BITS_8) == 0) {
        sfr_status.value = sfr_status.value | BIT_3;
    }

    // destination
    if ((opcode & BIT_8) == BIT_8) { // store in f
        file->value = (result & BITS_8) & file->write_mask;
    } else { // store in w
        w->value = (result & BITS_8);
    }
}


void instr_andwf(Register* w, uint16_t opcode) {
    uint8_t file_address = opcode & BITS_7;
    struct Register *file = &data_memory[(sfr_status.value & 0b01100000) >> 5][file_address];
    while (file->mirror != file) {
        file = file->mirror;
    }

    uint16_t result = (file->value & file->read_mask) & (w->value);

    // flags
    // z
    if ((result & BITS_8) == 0) {
        sfr_status.value = sfr_status.value | BIT_3;
    }

    // destination
    if ((opcode & BIT_8) == BIT_8) { // store in f
        file->value = (result & BITS_8) & file->write_mask;
    } else { // store in w
        w->value = (result & BITS_8);
    }
}


void instr_clrf(Register* w, uint16_t opcode) {
    uint8_t file_address = opcode & BITS_7;
    struct Register *file = &data_memory[(sfr_status.value & 0b01100000) >> 5][file_address];
    while (file->mirror != file) {
        file = file->mirror;
    }

    file->value = ~(BITS_8 & file->write_mask);

    // flags
    // z
    sfr_status.value = sfr_status.value | BIT_3;
}


void instr_clrw(Register* w, uint16_t opcode) {
    w->value = 0b0;

    // flags
    // z
    sfr_status.value = sfr_status.value | BIT_3;
}


void instr_comf(Register* w, uint16_t opcode) {
    uint8_t file_address = opcode & BITS_7;
    struct Register *file = &data_memory[(sfr_status.value & 0b01100000) >> 5][file_address];
    while (file->mirror != file) {
        file = file->mirror;
    }

    uint16_t result = ~(file->value);

    // flags
    // z
    if ((result & BITS_8) == 0) {
        sfr_status.value = sfr_status.value | BIT_3;
    }

    // destination
    if ((opcode & BIT_8) == BIT_8) { // store in f
        file->value = (result & BITS_8) & file->write_mask;
    } else { // store in w
        w->value = (result & BITS_8);
    }
}


void instr_decf(Register* w, uint16_t opcode) {
    uint8_t file_address = opcode & BITS_7;
    struct Register *file = &data_memory[(sfr_status.value & 0b01100000) >> 5][file_address];
    while (file->mirror != file) {
        file = file->mirror;
    }

    uint16_t result = file->value - 1;

    // flags
    // z
    if ((result & BITS_8) == 0) {
        sfr_status.value = sfr_status.value | BIT_3;
    }

    // destination
    if ((opcode & BIT_8) == BIT_8) { // store in f
        file->value = (result & BITS_8) & file->write_mask;
    } else { // store in w
        w->value = (result & BITS_8);
    }
}


void instr_decfsz(Register* w, uint16_t opcode) { // skip
    uint8_t file_address = opcode & BITS_7;
    struct Register *file = &data_memory[(sfr_status.value & 0b01100000) >> 5][file_address];
    while (file->mirror != file) {
        file = file->mirror;
    }

    uint16_t result = file->value - 1;

    // flags
    // external 2cycle flag
    if ((result & BITS_8) == 0) {
        flag_two_cycle = 0b11;
    }

    // destination
    if ((opcode & BIT_8) == BIT_8) { // store in f
        file->value = (result & BITS_8) & file->write_mask;
    } else { // store in w
        w->value = (result & BITS_8);
    }
}


void instr_incf(Register* w, uint16_t opcode) {
    uint8_t file_address = opcode & BITS_7;
    struct Register *file = &data_memory[(sfr_status.value & 0b01100000) >> 5][file_address];
    while (file->mirror != file) {
        file = file->mirror;
    }

    uint16_t result = file->value + 1;

    // flags
    // z
    if ((result & BITS_8) == 0) {
        sfr_status.value = sfr_status.value | BIT_3;
    }

    // destination
    if ((opcode & BIT_8) == BIT_8) { // store in f
        file->value = (result & BITS_8) & file->write_mask;
    } else { // store in w
        w->value = (result & BITS_8);
    }
}


void instr_incfsz(Register* w, uint16_t opcode) { // skip
    uint8_t file_address = opcode & BITS_7;
    struct Register *file = &data_memory[(sfr_status.value & 0b01100000) >> 5][file_address];
    while (file->mirror != file) {
        file = file->mirror;
    }

    uint16_t result = file->value + 1;

    // flags
    // internal 2cycle flag
    if ((result & BITS_8) == 0) {
        flag_two_cycle = 0b11;
    }

    // destination
    if ((opcode & BIT_8) == BIT_8) { // store in f
        file->value = (result & BITS_8) & file->write_mask;
    } else { // store in w
        w->value = (result & BITS_8);
    }
}


void instr_iorwf(Register* w, uint16_t opcode) {
    uint8_t file_address = opcode & BITS_7;
    struct Register *file = &data_memory[(sfr_status.value & 0b01100000) >> 5][file_address];
    while (file->mirror != file) {
        file = file->mirror;
    }

    uint16_t result = file->value | w->value;

    // flags
    // z
    if ((result & BITS_8) == 0) {
        sfr_status.value = sfr_status.value | BIT_3;
    }

    // destination
    if ((opcode & BIT_8) == BIT_8) { // store in f
        file->value = (result & BITS_8) & file->write_mask;
    } else { // store in w
        w->value = (result & BITS_8);
    }
}


void instr_movf(Register* w, uint16_t opcode) {
    uint8_t file_address = opcode & BITS_7;
    struct Register *file = &data_memory[(sfr_status.value & 0b01100000) >> 5][file_address];
    while (file->mirror != file) {
        file = file->mirror;
    }

    uint16_t result = file->value;

    // flags
    // z
    if ((result & BITS_8) == 0) {
        sfr_status.value = sfr_status.value | BIT_3;
    }

    // destination
    if ((opcode & BIT_8) == BIT_8) { // store in f
        file->value = (result & BITS_8) & file->write_mask;
    } else { // store in w
        w->value = (result & BITS_8);
    }
}


void instr_movwf(Register* w, uint16_t opcode) {
    uint8_t file_address = opcode & BITS_7;
    struct Register *file = &data_memory[(sfr_status.value & 0b01100000) >> 5][file_address];

    while (file->mirror != file) {
        file = file->mirror;
    }

    file->value = (w->value & file->write_mask);
}


void instr_nop(Register* w, uint16_t opcode) {
    // nothing!
}


void instr_rlf(Register* w, uint16_t opcode) {
    uint8_t file_address = opcode & BITS_7;
    struct Register *file = &data_memory[(sfr_status.value & 0b01100000) >> 5][file_address];
    while (file->mirror != file) {
        file = file->mirror;
    }

    uint16_t result = ((file->value & file->read_mask) << 1) + (sfr_status.value & BIT_1);

    // flags
    // c
    sfr_status.value = (sfr_status.value & (BITS_7 << 1)) + (result & BIT_9);

    // destination
    if ((opcode & BIT_8) == BIT_8) { // store in f
        file->value = (result & BITS_8) & file->write_mask;
    } else { // store in w
        w->value = (result & BITS_8);
    }
}


void instr_rrf(Register* w, uint16_t opcode) {
    uint8_t file_address = opcode & BITS_7;
    struct Register *file = &data_memory[(sfr_status.value & 0b01100000) >> 5][file_address];
    while (file->mirror != file) {
        file = file->mirror;
    }

    uint16_t result = file->value + ((sfr_status.value & BIT_1) << 8);

    // flags
    // c
    sfr_status.value = (sfr_status.value & (BITS_7 << 1)) + (result & BIT_1);

    result = result >> 1;

    // destination
    if ((opcode & BIT_8) == BIT_8) { // store in f
        file->value = (result & BITS_8) & file->write_mask;
    } else { // store in w
        w->value = (result & BITS_8);
    }
}


void instr_subwf(Register* w, uint16_t opcode) {
    uint8_t file_address = opcode & BITS_7;
    struct Register *file = &data_memory[(sfr_status.value & 0b01100000) >> 5][file_address];
    while (file->mirror != file) {
        file = file->mirror;
    }

    uint16_t result = (file->value & file->read_mask) + (~(w->value) + 1);

    // flags
    // c
    if ((result & BIT_9) == BIT_9) {
        sfr_status.value = sfr_status.value | BIT_1;
    }
    // dc
    if (((((file->value & file->read_mask) & BITS_4) + ~((w->value & BITS_4) + 1)) & BIT_5) == BIT_5) {
        sfr_status.value = sfr_status.value | BIT_2;
    }
    // z
    if ((result & BITS_8) == 0) {
        sfr_status.value = sfr_status.value | BIT_3;
    }

    // destination
    if ((opcode & BIT_8) == BIT_8) { // store in f
        file->value = (result & BITS_8) & file->write_mask;
    } else { // store in w
        w->value = (result & BITS_8);
    }
}


void instr_swapf(Register* w, uint16_t opcode) {
    uint8_t file_address = opcode & BITS_7;
    struct Register *file = &data_memory[(sfr_status.value & 0b01100000) >> 5][file_address];
    while (file->mirror != file) {
        file = file->mirror;
    }

    uint16_t result = (((file->value & file->read_mask) & BITS_4) << 4) | (((file->value & file->read_mask) >> 4) & BITS_4);

    // destination
    if ((opcode & BIT_8) == BIT_8) { // store in f
        file->value = (result & BITS_8) & file->write_mask;
    } else { // store in w
        w->value = (result & BITS_8);
    }
}


void instr_xorwf(Register* w, uint16_t opcode) {
    uint8_t file_address = opcode & BITS_7;
    struct Register *file = &data_memory[(sfr_status.value & 0b01100000) >> 5][file_address];
    while (file->mirror != file) {
        file = file->mirror;
    }

    uint16_t result = (file->value & file->read_mask) ^ w->value;

    // flags
    // z
    if ((result & BITS_8) == 0) {
        sfr_status.value = sfr_status.value | BIT_3;
    }

    // destination
    if ((opcode & BIT_8) == BIT_8) { // store in f
        file->value = (result & BITS_8) & file->write_mask;
    } else { // store in w
        w->value = (result & BITS_8);
    }
}


void instr_bcf(Register* w, uint16_t opcode) {
    uint8_t file_address = opcode & BITS_7;
    uint8_t file_bit = (opcode & 0b1110000000) >> 7;
    struct Register *file = &data_memory[(sfr_status.value & 0b01100000) >> 5][file_address];
    while (file->mirror != file) {
        file = file->mirror;
    }

    file->value = (file->value & file->write_mask) & ~(BIT_1 << file_bit);
}


void instr_bsf(Register* w, uint16_t opcode) {
    uint8_t file_address = opcode & BITS_7;
    uint8_t file_bit = (opcode & 0b1110000000) >> 7;
    struct Register *file = &data_memory[(sfr_status.value & 0b01100000) >> 5][file_address];
    while (file->mirror != file) {
        file = file->mirror;
    }

    file->value = (file->value & file->write_mask) | (BIT_1 << file_bit);
}


void instr_btfsc(Register* w, uint16_t opcode) { // skip
    uint8_t file_address = opcode & BITS_7;
    uint8_t file_bit = (opcode & 0b1110000000) >> 7;
    struct Register *file = &data_memory[(sfr_status.value & 0b01100000) >> 5][file_address];
    while (file->mirror != file) {
        file = file->mirror;
    }

    if (((BIT_1 << file_bit) & (file->value & file->read_mask)) == (BIT_1 << file_bit)) { // set
        // nothing
    } else { // clear
        // skip
        flag_two_cycle = 0b11;
    }
}


void instr_btfss(Register* w, uint16_t opcode) { // skip
    uint8_t file_address = opcode & BITS_7;
    uint8_t file_bit = (opcode & 0b1110000000) >> 7;
    struct Register *file = &data_memory[(sfr_status.value & 0b01100000) >> 5][file_address];
    while (file->mirror != file) {
        file = file->mirror;
    }

    if (((BIT_1 << file_bit) & (file->value & file->read_mask)) == (BIT_1 << file_bit)) { // set
        // skip
        flag_two_cycle = 0b11;
    } else { // clear
        // nothing
    }
}


void instr_addlw(Register* w, uint16_t opcode) {
    uint8_t literal = opcode & BITS_8;

    uint16_t result = literal + w->value;

    // flags
    // c
    if (result & BIT_9) {
        sfr_status.value = sfr_status.value | BIT_1;
    }
    // dc
    if ((((w->value & BITS_4) + (literal & BITS_4)) & BIT_5) == BIT_5) {
        sfr_status.value = sfr_status.value | BIT_2;
    }
    // z
    if ((result & BITS_8) == 0) {
        sfr_status.value = sfr_status.value | BIT_3;
    }

    w->value = result & BITS_8;
}


void instr_andlw(Register* w, uint16_t opcode) {
    uint8_t literal = opcode & BITS_8;

    uint16_t result = literal & w->value;

    // flags
    // z
    if ((result & BITS_8) == 0) {
        sfr_status.value = sfr_status.value | BIT_3;
    }

    w->value = result & BITS_8;
}


void instr_call(Register* w, uint16_t opcode) {
    push_pc_stack(&stkptr, program_counter + 1);

    uint16_t subroutine_destination = opcode & BITS_11;

    subroutine_destination = subroutine_destination | ((sfr_pcl.value & sfr_pcl.read_mask) & (BIT_4 | BIT_5));

    program_counter = subroutine_destination & BITS_13;

    flag_two_cycle = 0b01;
}


void instr_clrwdt(Register* w, uint16_t opcode) {

}


void instr_goto(Register* w, uint16_t opcode) {
    uint16_t literal = opcode & BITS_11;

    program_counter = literal & ((sfr_pclath.value & sfr_pclath.read_mask) & (BIT_4 | BIT_5));

    flag_two_cycle = 1;
}


void instr_iorlw(Register* w, uint16_t opcode) {
    uint8_t literal = opcode & BITS_8;

    uint16_t result = literal | w->value;

    // flags
    // z
    if ((result & BITS_8) == 0) {
        sfr_status.value = sfr_status.value | BIT_3;
    }

    w->value = result & BITS_8;
}


void instr_movlw(Register* w, uint16_t opcode) {
    uint8_t literal = opcode & BITS_8;

    w->value = literal;
}


void instr_retfie(Register* w, uint16_t opcode) {
    uint16_t return_vector = pop_pc_stack(&stkptr);

    program_counter = return_vector & BITS_13;
    sfr_intcon.value = sfr_intcon.value | BIT_8;

    flag_two_cycle = 0b01;
}


void instr_retlw(Register* w, uint16_t opcode) {
    uint16_t return_vector = pop_pc_stack(&stkptr);
    uint8_t literal = opcode & BITS_8;

    program_counter = return_vector & BITS_13;
    w->value = literal;

    flag_two_cycle = 0b01;
}


void instr_return(Register* w, uint16_t opcode) {
    uint16_t return_vector = pop_pc_stack(&stkptr);

    program_counter = return_vector & BITS_13;

    flag_two_cycle = 0b01;
}


void instr_sleep(Register* w, uint16_t opcode) {

}


void instr_sublw(Register* w, uint16_t opcode) {
    uint8_t literal = opcode & BITS_8;

    uint16_t result = literal + (~(w->value) + 1);

    // flags
    // c
    if ((result & BIT_9) == BIT_9) {
        sfr_status.value = sfr_status.value | BIT_1;
    }
    // dc
    if ((((literal & BITS_4) + ~((w->value & BITS_4) + 1)) & BIT_5) == BIT_5) {
        sfr_status.value = sfr_status.value | BIT_2;
    }
    // z
    if ((result & BITS_8) == 0) {
        sfr_status.value = sfr_status.value | BIT_3;
    }

    w->value = result & BITS_8;
}


void instr_xorlw(Register* w, uint16_t opcode) {
    uint8_t literal = opcode & BITS_8;

    uint16_t result = literal ^ w->value;

    // flags
    // z
    if ((result & BITS_8) == 0) {
        sfr_status.value = sfr_status.value | BIT_3;
    }

    w->value = result & BITS_8;
}


// instruction register
typedef struct {
    uint16_t mask;
    uint16_t value;
    InstructionHandler handler;
} Instruction;

Instruction instructions[INSTRUCTIONS_COUNT] = {
    {0b11111100000000, 0b00011100000000, instr_addwf},
    {0b11111100000000, 0b00010100000000, instr_andwf},
    {0b11111110000000, 0b00000110000000, instr_clrf},
    {0b11111100000000, 0b00000100000000, instr_clrw},
    {0b11111100000000, 0b00100100000000, instr_comf},
    {0b11111100000000, 0b00001100000000, instr_decf},
    {0b11111100000000, 0b00101100000000, instr_decfsz},
    {0b11111100000000, 0b00101000000000, instr_incf},
    {0b11111100000000, 0b00111100000000, instr_incfsz},
    {0b11111100000000, 0b00010000000000, instr_iorwf},
    {0b11111100000000, 0b00100000000000, instr_movf},
    {0b11111110000000, 0b00000010000000, instr_movwf},
    {0b11111110011111, 0b00000000000000, instr_nop},
    {0b11111100000000, 0b00110100000000, instr_rlf},
    {0b11111100000000, 0b00110000000000, instr_rrf},
    {0b11111100000000, 0b00001000000000, instr_subwf},
    {0b11111100000000, 0b00111000000000, instr_swapf},
    {0b11111100000000, 0b00011000000000, instr_xorwf},

    {0b11110000000000, 0b01000000000000, instr_bcf},
    {0b11110000000000, 0b01010000000000, instr_bsf},
    {0b11110000000000, 0b01100000000000, instr_btfsc},
    {0b11110000000000, 0b01110000000000, instr_btfss},

    {0b11111000000000, 0b11111000000000, instr_addlw},
    {0b11111100000000, 0b11100100000000, instr_andlw},
    {0b11100000000000, 0b10000000000000, instr_call},
    {0b11111111111111, 0b00000001100100, instr_clrwdt},
    {0b11100000000000, 0b10100000000000, instr_goto},
    {0b11111100000000, 0b11100000000000, instr_iorlw},
    {0b11110000000000, 0b11000000000000, instr_movlw},
    {0b11111111111111, 0b00000000001001, instr_retfie},
    {0b11110000000000, 0b11010000000000, instr_retlw},
    {0b11111111111111, 0b00000000001000, instr_return},
    {0b11111111111111, 0b00000000110011, instr_sleep},
    {0b11111000000000, 0b11110000000000, instr_sublw},
    {0b11111100000000, 0b11101000000000, instr_xorlw},
};


void _init_data_mem() {
    // fill data_memory
    // GPRs treated as their own register, and mirror points to themselves (other than exceptions in banks 1,2,3)
    // SFRs given null register with mirror pointing to variable for literal writes to SFR

    // GPRs between 0x20 and 0x70
    for (int i = 0x20; i < 0x70; i++) {
        data_memory[0][i].mirror = &data_memory[0][i]; data_memory[0][i].write_mask = 0b11111111; data_memory[0][i].read_mask = 0b11111111;
        data_memory[1][i].mirror = &data_memory[1][i]; data_memory[1][i].write_mask = 0b11111111; data_memory[1][i].read_mask = 0b11111111;
        data_memory[2][i].mirror = &data_memory[2][i]; data_memory[2][i].write_mask = 0b11111111; data_memory[2][i].read_mask = 0b11111111;
        data_memory[3][i].mirror = &unimplemented;
    }

    // final GPRs in bank0 and mirrors for other 3 banks to bank 0
    for (int i = 0x70; i < 0x80; i++) {
        data_memory[0][i].mirror = &data_memory[0][i]; data_memory[0][i].write_mask = 0b11111111; data_memory[0][i].read_mask = 0b11111111;
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


void instruction_decode() {
    for (int i = 0; i < INSTRUCTIONS_COUNT; i++) {
        if ((instruction_register & instructions[i].mask) == instructions[i].value) {
            instructions[i].handler(&w, instruction_register);
            break;
        }
    }
}


// modules
//--timer0
void module_timer0() {

}


void setup() {
    _init_data_mem();
    
    for (int i = 0; i < 4; i++) {
        program_memory[i + 0x20] = demo_1[i];
    }
}


void loop() {
    for (int i = 0; i < 4; i++) {
        if (flag_two_cycle) { // previous instruction is two-cycle, skip incrementing pc
            // instrs nopping next instruction
            // decfsz, incfsz, btfsc, btfss
            if (flag_two_cycle & BIT_2) {
                program_counter++;
            }
            flag_two_cycle = 0b00;
        } else {
            // load instruction todo: retlw and other exceptions to do with stack
            instruction_register = program_memory[program_counter];
            instruction_decode();

            // increment pc
            program_counter += 1;
        }
        printf("Intruction %d\nW value: %b\n0x21 Register value: %b\nStatus: %b\n\n", i, w.value, data_memory[0][0x21].value, sfr_status.value);
    }
}


int main() {
    setup();
    loop();
    return 0;
}
