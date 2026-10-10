// US Dark Cloud 2 HDD support, compact loader.
// Only boot-time IOP driver startup and original InitCDFile continuation.
// The 0x01FE8000 temporary EE slot remains an unverified lifetime constraint.
.set noreorder
.set nomacro
.text
.align 4
.globl _start
_start:
    addiu $sp, $sp, -192
    sd $ra, 0xB8($sp)
    sd $gp, 0xB0($sp)
    sd $s0, 0xA8($sp)
    sd $s1, 0xA0($sp)
    sd $s2, 0x98($sp)
    sd $s3, 0x90($sp)
    sd $a0, 0x88($sp)
    sd $a1, 0x80($sp)
    sd $a2, 0x78($sp)
    sd $a3, 0x70($sp)

// DEV9
    sw $zero, 0x20($sp)
    lui $a0, %hi(path_dev9)
    addiu $a0, $a0, %lo(path_dev9)
    addiu $a1, $zero, 0
    or $a2, $zero, $zero
    addiu $a3, $sp, 0x20
    jal 0x00117D58
    nop
    bltz $v0, call_original
    nop
    lw $t9, 0x20($sp)
    addiu $t0, $zero, 1
    beq $t9, $t0, call_original
    nop

// ATAD
    sw $zero, 0x20($sp)
    lui $a0, %hi(path_atad)
    addiu $a0, $a0, %lo(path_atad)
    addiu $a1, $zero, 0
    or $a2, $zero, $zero
    addiu $a3, $sp, 0x20
    jal 0x00117D58
    nop
    bltz $v0, call_original
    nop
    lw $t9, 0x20($sp)
    addiu $t0, $zero, 1
    beq $t9, $t0, call_original
    nop

// HDD: "-o\0" "2\0" "-n\0" "20\0"
    sw $zero, 0x20($sp)
    lui $a0, %hi(path_hdd)
    addiu $a0, $a0, %lo(path_hdd)
    addiu $a1, $zero, 11
    lui $a2, %hi(args_hdd)
    addiu $a2, $a2, %lo(args_hdd)
    addiu $a3, $sp, 0x20
    jal 0x00117D58
    nop
    bltz $v0, call_original
    nop
    lw $t9, 0x20($sp)
    addiu $t0, $zero, 1
    beq $t9, $t0, call_original
    nop

// PFS: "-m\0" "2\0" "-o\0" "16\0" "-n\0" "40\0"
    sw $zero, 0x20($sp)
    lui $a0, %hi(path_pfs)
    addiu $a0, $a0, %lo(path_pfs)
    addiu $a1, $zero, 17
    lui $a2, %hi(args_pfs)
    addiu $a2, $a2, %lo(args_pfs)
    addiu $a3, $sp, 0x20
    jal 0x00117D58
    nop
    bltz $v0, call_original
    nop
    lw $t9, 0x20($sp)
    addiu $t0, $zero, 1
    beq $t9, $t0, call_original
    nop

    jal 0x00114008     // sceFsInit
    nop

call_original:
    ld $a0, 0x88($sp)
    ld $a1, 0x80($sp)
    ld $a2, 0x78($sp)
    ld $a3, 0x70($sp)
    jal 0x00148F70     // InitCDFile, original call
    nop
    ld $s3, 0x90($sp)
    ld $s2, 0x98($sp)
    ld $s1, 0xA0($sp)
    ld $s0, 0xA8($sp)
    ld $gp, 0xB0($sp)
    ld $ra, 0xB8($sp)
    addiu $sp, $sp, 192
    jr $ra
    nop

.align 4
path_dev9: .asciz "cdrom0:\\MODULES\\DEV9.IRX;1"
path_atad: .asciz "cdrom0:\\MODULES\\ATAD.IRX;1"
path_hdd:  .asciz "cdrom0:\\MODULES\\HDD.IRX;1"
path_pfs:  .asciz "cdrom0:\\MODULES\\PFS.IRX;1"
args_hdd:  .byte 0x2D,0x6F,0,0x32,0,0x2D,0x6E,0,0x32,0x30,0
args_pfs:  .byte 0x2D,0x6D,0,0x32,0,0x2D,0x6F,0,0x31,0x36,0,0x2D,0x6E,0,0x34,0x30,0
.align 2
