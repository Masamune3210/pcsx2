// AnnotatePsbbnVmlinuxTlb_20261017_v1.java
// @category PS2
// Evidence-only annotations for Sony PSBBN 0.32's vmlinux (Linux 2.4.17_mvl21).
// Run with this exact vmlinux ELF open in Ghidra, after symbol import/auto-analysis.
// Idempotent: bookmarks are replaced by category, and comments have a unique marker.
// No program bytes, function signatures, or function names are changed.

import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.Address;
import ghidra.program.model.listing.CodeUnit;
import ghidra.program.model.listing.Listing;
import java.util.Arrays;

public class AnnotatePsbbnVmlinuxTlb_20261017_v1 extends GhidraScript {
    private static final String MARK = "[PSBBN-TLB 20261017]";
    private static final String CATEGORY = "PSBBN TLB Investigation";

    private Address address(long value) {
        return toAddr(value);
    }

    private void verifyInstruction(long addressValue, byte[] expected) throws Exception {
        byte[] actual = new byte[expected.length];
        int count = currentProgram.getMemory().getBytes(address(addressValue), actual);
        if (count != expected.length || !Arrays.equals(actual, expected)) {
            throw new IllegalStateException(String.format(
                "Unexpected bytes at 0x%08X. This may be the wrong ELF or image base.", addressValue));
        }
    }

    private void annotate(long addressValue, String subject, String evidence) {
        Address at = address(addressValue);
        Listing listing = currentProgram.getListing();
        String note = MARK + " " + subject + "\n" + evidence;
        CodeUnit unit = listing.getCodeUnitAt(at);
        if (unit != null) {
            String old = unit.getComment(CodeUnit.PLATE_COMMENT);
            if (old == null) {
                unit.setComment(CodeUnit.PLATE_COMMENT, note);
            } else if (!old.contains(MARK)) {
                unit.setComment(CodeUnit.PLATE_COMMENT, old + "\n\n" + note);
            }
        }
        currentProgram.getBookmarkManager().setBookmark(at, "Note", CATEGORY, note);
        println(String.format("0x%08X %s", addressValue, subject));
    }

    @Override
    public void run() throws Exception {
        verifyInstruction(0x801A4930L,
            new byte[] {(byte)0x40, 0x00, (byte)0x84, 0x24});
        verifyInstruction(0x801A4934L,
            new byte[] {(byte)0xC0, (byte)0xFF, (byte)0x85, (byte)0xAC});
        verifyInstruction(0x800258B8L,
            new byte[] {0x06, 0x00, 0x00, 0x42});
        verifyInstruction(0x80025A08L,
            new byte[] {0x06, 0x00, 0x00, 0x42});

        annotate(0x80010490L, "kernel_entry",
            "ELF e_entry=0x80010490; physical LOAD origin 0x80010000. Kernel has full ELF symbol table.");
        annotate(0x801A48E0L, "memset entry",
            "ELF .symtab: memset at 0x801A48E0 (304-byte function); __bzero shares its body at 0x801A48FC.");
        annotate(0x801A48FCL, "__bzero shared entry",
            "Kernel symbol __bzero; optimized fill loop begins at 0x801A4930. Avoid creating a conflicting function body.");
        annotate(0x801A4930L, "__bzero loop header, reported PC",
            "2026-10-17 EE recompiler: TLB Miss pc=0x801A4930, stores 0xC0005000, 0xC0005004, ...\n"
            + "Instruction at this address is ADDIU a0,a0,0x40, not a memory store; host-recompiler PC can be a block header.");
        annotate(0x801A4934L, "__bzero first store",
            "SW a1,-0x40(a0); fifteen more 4-byte stores follow, through 0x801A4970.\n"
            + "The 64-byte block zeroing explains the sequential TLB miss addresses.");
        annotate(0x80025790L, "update_mmu_cache",
            "Linux kernel MMU cache function; inspect its TLBP/TLBWR/TLBWI sequence to map fault handling.");
        annotate(0x800258B8L, "TLBWR in update_mmu_cache",
            "One of exactly two statically encoded 0x42000006 TLBWR instructions in vmlinux .text.\n"
            + "The interpreter log does not give guest PC, so it has NOT been proven this is the repeated TLBWR.");
        annotate(0x800259E0L, "except_vec0_r5900_spr",
            "R5900 scratchpad refill exception-vector template. This may be copied to a live vector location.");
        annotate(0x80025A08L, "TLBWR in SPR refill vector",
            "Second and only other static 0x42000006 TLBWR instruction in vmlinux .text.\n"
            + "It occurs in a branch delay slot. Preceding code writes EntryLo0=0x80000006 and EntryLo1=0x6;\n"
            + "interpreter output EntryLo0=EntryLo1=0 is not enough to attribute the log to this site.");
        annotate(0x80025A20L, "handle_tlbl entry",
            "Linux TLB load/refill handler; compare guest COP0 EPC/Cause/BadVAddr before editing MMU behavior.");
        annotate(0x80025BC0L, "handle_tlbs entry",
            "Linux TLB store/refill handler. Kernel writes to 0xC0005000 should be correlated with page-table entries.");
        annotate(0x8021DB48L, "pgd_current",
            "ELF symbol: pgd_current, OBJECT size=8, in .bss. Preserve existing datatype unless established.");
        println("PSBBN TLB annotations complete. No renames or type changes made.");
    }
}
