// Known-good v1 payload. Do not change instruction/data layout without a boot regression.
// Built at 0x01FE8000. Loader returns to original InitCDFile call path.
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
  lui $t8, %hi(dc2_stage)
  addiu $t8, $t8, %lo(dc2_stage)
  sw $zero, 0($t8)
  lui $t8, %hi(dc2_failure)
  addiu $t8, $t8, %lo(dc2_failure)
  sw $zero, 0($t8)
load_dev9:
  sw $zero, 0x20($sp)
  lui $a0, %hi(path_dev9)
  addiu $a0, $a0, %lo(path_dev9)
  addiu $a1, $zero, 0
  or $a2, $zero, $zero
  addiu $a3, $sp, 0x20
  jal 0x00117D58
  nop
  lui $t8, %hi(dc2_dev9_id)
  addiu $t8, $t8, %lo(dc2_dev9_id)
  sw $v0, 0($t8)
  bltz $v0, load_failed
  nop
  lw $t9, 0x20($sp)
  addiu $t0, $zero, 1
  beq $t9, $t0, load_failed
  nop
  lui $t8, %hi(dc2_stage)
  addiu $t8, $t8, %lo(dc2_stage)
  addiu $t0, $zero, 1
  sw $t0, 0($t8)
load_atad:
  sw $zero, 0x20($sp)
  lui $a0, %hi(path_atad)
  addiu $a0, $a0, %lo(path_atad)
  addiu $a1, $zero, 0
  or $a2, $zero, $zero
  addiu $a3, $sp, 0x20
  jal 0x00117D58
  nop
  lui $t8, %hi(dc2_atad_id)
  addiu $t8, $t8, %lo(dc2_atad_id)
  sw $v0, 0($t8)
  bltz $v0, load_failed
  nop
  lw $t9, 0x20($sp)
  addiu $t0, $zero, 1
  beq $t9, $t0, load_failed
  nop
  lui $t8, %hi(dc2_stage)
  addiu $t8, $t8, %lo(dc2_stage)
  addiu $t0, $zero, 2
  sw $t0, 0($t8)
load_hdd:
  sw $zero, 0x20($sp)
  lui $a0, %hi(path_hdd)
  addiu $a0, $a0, %lo(path_hdd)
  addiu $a1, $zero, 11
  lui $a2, %hi(args_hdd)
  addiu $a2, $a2, %lo(args_hdd)
  addiu $a3, $sp, 0x20
  jal 0x00117D58
  nop
  lui $t8, %hi(dc2_hdd_id)
  addiu $t8, $t8, %lo(dc2_hdd_id)
  sw $v0, 0($t8)
  bltz $v0, load_failed
  nop
  lw $t9, 0x20($sp)
  addiu $t0, $zero, 1
  beq $t9, $t0, load_failed
  nop
  lui $t8, %hi(dc2_stage)
  addiu $t8, $t8, %lo(dc2_stage)
  addiu $t0, $zero, 3
  sw $t0, 0($t8)
load_pfs:
  sw $zero, 0x20($sp)
  lui $a0, %hi(path_pfs)
  addiu $a0, $a0, %lo(path_pfs)
  addiu $a1, $zero, 17
  lui $a2, %hi(args_pfs)
  addiu $a2, $a2, %lo(args_pfs)
  addiu $a3, $sp, 0x20
  jal 0x00117D58
  nop
  lui $t8, %hi(dc2_pfs_id)
  addiu $t8, $t8, %lo(dc2_pfs_id)
  sw $v0, 0($t8)
  bltz $v0, load_failed
  nop
  lw $t9, 0x20($sp)
  addiu $t0, $zero, 1
  beq $t9, $t0, load_failed
  nop
  lui $t8, %hi(dc2_stage)
  addiu $t8, $t8, %lo(dc2_stage)
  addiu $t0, $zero, 4
  sw $t0, 0($t8)
fs_init:
  jal 0x00114008
  nop
  lui $t8, %hi(dc2_fs_result)
  addiu $t8, $t8, %lo(dc2_fs_result)
  sw $v0, 0($t8)
  bltz $v0, load_failed
  nop
  lui $t8, %hi(dc2_stage)
  addiu $t8, $t8, %lo(dc2_stage)
  addiu $t0, $zero, 5
  sw $t0, 0($t8)
  b call_original
  nop
load_failed:
  lui $t8, %hi(dc2_failure)
  addiu $t8, $t8, %lo(dc2_failure)
  sw $v0, 0($t8)
call_original:
  ld $a0, 0x88($sp)
  ld $a1, 0x80($sp)
  ld $a2, 0x78($sp)
  ld $a3, 0x70($sp)
  jal 0x00148F70
  nop
  lui $t8, %hi(dc2_complete)
  addiu $t8, $t8, %lo(dc2_complete)
  addiu $t0, $zero, 1
  sw $t0, 0($t8)
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
dc2_loader_data_start:
path_dev9:
  .ascii "cdrom0:\\MODULES\\DEV9.IRX;1"
  .byte 0
path_atad:
  .ascii "cdrom0:\\MODULES\\ATAD.IRX;1"
  .byte 0
path_hdd:
  .ascii "cdrom0:\\MODULES\\HDD.IRX;1"
  .byte 0
path_pfs:
  .ascii "cdrom0:\\MODULES\\PFS.IRX;1"
  .byte 0
args_hdd:
  .byte 0x2D, 0x6F, 0x00, 0x32, 0x00, 0x2D, 0x6E, 0x00, 0x32, 0x30, 0x00
args_pfs:
  .byte 0x2D, 0x6D, 0x00, 0x32, 0x00, 0x2D, 0x6F, 0x00, 0x31, 0x36, 0x00, 0x2D, 0x6E, 0x00, 0x34, 0x30, 0x00
.align 2
dc2_status_header:
  .ascii "DC2HDDv1"
.align 2
dc2_stage:
  .word 0xFFFFFFFF
dc2_failure:
  .word 0xFFFFFFFF
dc2_fs_result:
  .word 0x7FFFFFFF
dc2_complete:
  .word 0
dc2_dev9_id:
  .word 0x7FFFFFFF
dc2_atad_id:
  .word 0x7FFFFFFF
dc2_hdd_id:
  .word 0x7FFFFFFF
dc2_pfs_id:
  .word 0x7FFFFFFF
dc2_loader_data_end:
