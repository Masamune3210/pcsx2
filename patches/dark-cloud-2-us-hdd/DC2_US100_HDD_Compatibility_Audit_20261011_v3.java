// @category PS2.DarkCloud2
// Ghidra 12.1.3: audit that the US v1.00 SCUS_972.13 ELF is the same
// executable targeted by the PCSX2-stable US v2.00 native HDD patch.
// Guarded: writes only non-destructive explanatory PLATE comments after
// verifying Ghidra's original executable hashes and every original hook byte.

import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.Address;
import ghidra.program.model.listing.CommentType;
import ghidra.program.model.listing.Function;
import ghidra.program.model.listing.Listing;
import ghidra.program.model.mem.Memory;

public class DC2_US100_HDD_Compatibility_Audit_20261011_v3 extends GhidraScript {
    private static final String SHA = "ac784f749c046cc78ebc3d7a026342a505354c422ffc55187b8128983d7d74e7";
    private static final String MD5 = "2d1aca0b2522a520a4e8839e88b7db69";
    private static final String TAG = "[DC2_US100_HDD_ELF_VERIFIED_20261011]";
    private static final long[][] WORDS = {
        {0x0015C374L, 0x0C0523DCL}, // jal InitCDFile @ 0x00148F70
        {0x0015C398L, 0x00000000L}, // unused alignment padding
        {0x0015C39CL, 0x00000000L}, // unused padding
        {0x0029F2ECL, 0xA3808460L}, // clear language-dependent HDD title flag
        {0x0031C4BCL, 0x2A42000BL}, // original 11 extra partitions
        {0x0031BF40L, 0x2A220600L}  // original 1536 MiB space check
    };
    private static final String[][] FUNCS = {
        {"0015C160", "init"}, {"00148F70", "InitCDFile"},
        {"00117D58", "sceSifLoadStartModule"},
        {"00114008", "sceFsInit"}, {"0029F1F0", "TitleInit"},
        {"0031BE60", "CheckInstallSpace"}, {"0031C270", "InstallLoop"}
    };
    private Address address(long off) {
        return currentProgram.getAddressFactory().getDefaultAddressSpace().getAddress(off);
    }
    @Override
    protected void run() throws Exception {
        println("Dark Cloud 2 US 1.00/2.00 native HDD executable compatibility");
        println("File: " + currentProgram.getExecutablePath());
        println("MD5: " + currentProgram.getExecutableMD5());
        println("SHA256: " + currentProgram.getExecutableSHA256());
        boolean good = "SCUS_972.13".equals(currentProgram.getName())
            && "r5900:LE:32:default".equals(currentProgram.getLanguageID().getIdAsString())
            && currentProgram.getImageBase().getOffset() == 0x00100000L
            && MD5.equalsIgnoreCase(currentProgram.getExecutableMD5())
            && SHA.equalsIgnoreCase(currentProgram.getExecutableSHA256());
        if (!good) {
            printerr("REJECT: program identity/hash mismatch. No database changes.");
            return;
        }
        Memory memory = currentProgram.getMemory();
        for (long[] entry : WORDS) {
            monitor.checkCancelled();
            long actual = memory.getInt(address(entry[0])) & 0xffffffffL;
            boolean match = actual == entry[1];
            println(String.format("0x%08X: 0x%08X expected 0x%08X %s",
                entry[0], actual, entry[1], match ? "OK" : "MISMATCH"));
            good &= match;
        }
        if (!good) {
            printerr("REJECT: original patch-site instruction mismatch. No DB changes.");
            return;
        }
        for (String[] spec : FUNCS) {
            monitor.checkCancelled();
            Address a = address(Long.parseLong(spec[0], 16));
            Function f = currentProgram.getFunctionManager().getFunctionAt(a);
            println("FUNC " + a + " expected " + spec[1] + " -> " +
                (f == null ? "<unresolved>" : f.getPrototypeString(true, true)));
        }
        String[][] annotations = {
            {"0015C374", "Original InitCDFile call. HDD patch routes through 0015C398, 01FE8000, then the original function at 00148F70."},
            {"0029F2EC", "Language-dependent US title HDD gate store; patch NOPs it without forging HDD presence."},
            {"0031C4BC", "HDD installer partition loop bound 11. Production patch raises to 12 (1664 MiB total)."},
            {"0031BF40", "HDD install free-space threshold 1536 MiB. Production patch raises to 1664 MiB."}
        };
        int transaction = currentProgram.startTransaction("Tag verified Dark Cloud 2 HDD patch sites");
        boolean commit = false;
        int count = 0;
        try {
            Listing listing = currentProgram.getListing();
            for (String[] annotation : annotations) {
                monitor.checkCancelled();
                Address a = address(Long.parseLong(annotation[0], 16));
                String old = listing.getComment(CommentType.PLATE, a);
                if (old != null && old.contains(TAG)) continue;
                listing.setComment(a, CommentType.PLATE,
                    (old == null || old.isEmpty() ? "" : old + "\n") +
                    TAG + " " + annotation[1]);
                count++;
            }
            commit = true;
        } finally {
            currentProgram.endTransaction(transaction, commit);
        }
        println("PASS: 6 original patch-site words + MD5 + SHA256 all verified.");
        println("Ghidra comments added: " + count + ". No code, data, or function signatures changed.");
        println("Existing PCSX2 patch: 1DF41F33.pnach, HDD Support group, 127 word writes.");
        println("IRX assets in the US v1.00 disc have NOT been compared/boot-tested.");
    }
}
