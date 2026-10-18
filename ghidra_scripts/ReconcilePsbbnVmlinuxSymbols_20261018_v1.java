// ReconcilePsbbnVmlinuxSymbols_20261018_v1.java
// @category PS2
// Import exact function/label addresses from the original PSBBN 0.32 ELF .symtab
// into the user's existing Ghidra program. Complements CCC .mdebug import:
// never invents missing parameter/return types or overrides an existing function body.
// Choose the original unencrypted 'vmlinux' ELF when prompted. Not the .gzf archive.
// This is a bulk, conservative reconciliation and TLB annotation script.
//
// Import-order independence: No custom datatypes needed. Existing function names,
// signatures, user-defined symbols, and datatypes remain intact.

import ghidra.app.script.GhidraScript;
import ghidra.app.cmd.function.CreateFunctionCmd;
import ghidra.program.model.address.Address;
import ghidra.program.model.address.AddressSet;
import ghidra.program.model.listing.CodeUnit;
import ghidra.program.model.listing.Function;
import ghidra.program.model.listing.Instruction;
import ghidra.program.model.mem.MemoryBlock;
import ghidra.program.model.symbol.SourceType;
import ghidra.program.model.symbol.Symbol;
import ghidra.program.model.symbol.SymbolIterator;
import java.io.File;
import java.nio.charset.StandardCharsets;
import java.nio.file.Files;
import java.security.MessageDigest;
import java.util.Locale;

public class ReconcilePsbbnVmlinuxSymbols_20261018_v1 extends GhidraScript {
    private static final String EXPECTED_SHA256 =
        "07e44b50e2558d188caa306f60c138ad6015bbabcd3bc32fba35eeb6e8a870fc";
    private static final String TAG = "[PSBBN vmlinux 20261018]";
    private static final String CATEGORY = "PSBBN Linux kernel";
    private int already, functionsCreated, functionsRenamed, labelsCreated, overlaps, badNames, unmapped;
    private int functionsExamined, objectsExamined, emptyAliases;

    private static int u8(byte[] b, int o) {
        if (o < 0 || o >= b.length) throw new IllegalArgumentException("out-of-range ELF byte");
        return b[o] & 255;
    }
    private static int u16(byte[] b, int o) {
        return u8(b,o) | (u8(b,o+1)<<8);
    }
    private static long u32(byte[] b, int o) {
        return ((long)u16(b,o) | ((long)u16(b,o+2)<<16)) & 0xffffffffL;
    }
    private static String nulString(byte[] b, long at, long end) {
        if (at < 0 || at >= b.length || end < at || end > b.length) return "";
        int e=(int)at;
        while (e < end && b[e] != 0) ++e;
        return new String(b,(int)at,e-(int)at,StandardCharsets.UTF_8);
    }
    private static String sha256(byte[] b) throws Exception {
        byte[] d=MessageDigest.getInstance("SHA-256").digest(b);
        StringBuilder s=new StringBuilder();
        for (byte v:d) s.append(String.format(Locale.ROOT,"%02x",v & 255));
        return s.toString();
    }
    private Address at(long offset) {
        return toAddr(offset);
    }
    private boolean hasNameAt(Address a,String name) {
        SymbolIterator it=currentProgram.getSymbolTable().getSymbolsAsIterator(a);
        while(it.hasNext()) if (it.next().getName().equals(name)) return true;
        return false;
    }
    private void label(Address a,String name) {
        if (hasNameAt(a,name)) {already++;return;}
        try {
            currentProgram.getSymbolTable().createLabel(a,name,SourceType.IMPORTED);
            labelsCreated++;
        } catch (Exception e) {badNames++;}
    }
    private void reconcileFunction(long vaddr,long size,String name) {
        Address a=at(vaddr);
        MemoryBlock block=currentProgram.getMemory().getBlock(a);
        if (block==null || !block.isExecute() || !currentProgram.getMemory().contains(a)) {
            unmapped++;
            return;
        }
        if (size==0) {
            // e.g. __bzero is a legitimate alias inside the memset function.
            label(a,name);
            emptyAliases++;
            return;
        }
        Function f=currentProgram.getFunctionManager().getFunctionAt(a);
        if (f==null) {
            Function enclosing=currentProgram.getFunctionManager().getFunctionContaining(a);
            if (enclosing!=null) {
                // Never destroy/shorten a function Ghidra has already built.
                overlaps++;
                label(a,name);
                return;
            }
            Instruction inst=currentProgram.getListing().getInstructionAt(a);
            if (inst==null) disassemble(a);
            CreateFunctionCmd cmd=new CreateFunctionCmd(new AddressSet(a),SourceType.ANALYSIS);
            if (cmd.applyTo(currentProgram,monitor)) {
                f=currentProgram.getFunctionManager().getFunctionAt(a);
                if (f!=null) functionsCreated++;
            }
        }
        if (f==null) {
            // A valid ELF address need not be a valid current Ghidra function body.
            label(a,name);
            return;
        }
        if (f.getName().equals(name)) {already++;return;}
        if (f.getSymbol().getSource()!=SourceType.USER_DEFINED &&
            (f.getName().startsWith("FUN_") || f.getName().startsWith("LAB_") ||
             f.getName().startsWith("sub_"))) {
            try {f.setName(name,SourceType.IMPORTED); functionsRenamed++;return;}
            catch(Exception ignored) {}
        }
        label(a,name);
    }
    private void annotate(long vaddr,String title,String detail) {
        Address addr=at(vaddr);
        if (!currentProgram.getMemory().contains(addr)) {println("UNMAPPED annotation "+title);return;}
        String note=TAG+" "+title+"\n"+detail;
        CodeUnit code=currentProgram.getListing().getCodeUnitAt(addr);
        if (code!=null) {
            String old=code.getComment(CodeUnit.PLATE_COMMENT);
            if (old==null) code.setComment(CodeUnit.PLATE_COMMENT,note);
            else if (!old.contains(TAG+" "+title)) code.setComment(CodeUnit.PLATE_COMMENT,old+"\n\n"+note);
        }
        currentProgram.getBookmarkManager().setBookmark(addr,"Note",CATEGORY,note);
    }
    private void checkBytes(long va, int... expected) {
        Address a=at(va);
        for (int i=0;i<expected.length;i++) {
            if ((currentProgram.getMemory().getByte(a.add(i)) & 255) != expected[i])
                throw new IllegalStateException(String.format("Kernel opcode mismatch at %08X",va+i));
        }
    }
    @Override public void run() throws Exception {
        File input=askFile("Select ORIGINAL PSBBN 0.32 vmlinux ELF (not GZF)","Open");
        byte[] elf=Files.readAllBytes(input.toPath());
        String digest=sha256(elf);
        if (!EXPECTED_SHA256.equals(digest))
            throw new IllegalStateException("ELF SHA-256 mismatch: "+digest+" (expected "+EXPECTED_SHA256+")");
        if (u32(elf,0)!=0x464c457fL || u8(elf,4)!=1 || u8(elf,5)!=1 || u16(elf,18)!=8)
            throw new IllegalStateException("Expected little-endian ELF32 EM_MIPS");
        if (u32(elf,24)!=0x80010490L)
            throw new IllegalStateException("Unexpected PSBBN kernel entry");
        // Verify this Ghidra program matches the selected ELF before ANY mutation.
        checkBytes(0x801a4930L,0x40,0x00,0x84,0x24);
        checkBytes(0x801a4934L,0xc0,0xff,0x85,0xac);
        checkBytes(0x800258b8L,0x06,0x00,0x00,0x42);
        checkBytes(0x80025a08L,0x06,0x00,0x00,0x42);

        long shoff=u32(elf,32);
        int entsz=u16(elf,46), count=u16(elf,48);
        if (entsz!=40 || count<1 || count>128 || shoff+((long)count*entsz)>elf.length)
            throw new IllegalStateException("Unexpected ELF section-header table");
        int symhdr=-1;
        for (int i=0;i<count;i++) {
            int o=(int)(shoff+i*40);
            if (u32(elf,o+4)==2) { symhdr=o;break; }
        }
        if (symhdr<0) throw new IllegalStateException("ELF has no .symtab");
        long symoff=u32(elf,symhdr+16), symsize=u32(elf,symhdr+20);
        int strIndex=(int)u32(elf,symhdr+24);
        int symEntsz=(int)u32(elf,symhdr+36);
        if (strIndex>=count || symEntsz!=16 || symoff+symsize>elf.length)
            throw new IllegalStateException("Invalid ELF .symtab");
        int stro=(int)(shoff+strIndex*40);
        long strBase=u32(elf,stro+16), strSize=u32(elf,stro+20);
        if (strBase+strSize>elf.length) throw new IllegalStateException("Invalid .strtab");

        int n=(int)(symsize/16);
        for (int i=0;i<n;i++) {
            if (monitor.isCancelled()) break;
            int o=(int)symoff+i*16;
            long no=u32(elf,o);
            if (no==0 || no>=strSize) continue;
            String name=nulString(elf,strBase+no,strBase+strSize);
            if (name.isEmpty() || name.indexOf(' ')>=0) continue;
            long vaddr=u32(elf,o+4), size=u32(elf,o+8);
            int info=u8(elf,o+12), type=info&15, secIndex=u16(elf,o+14);
            if (secIndex==0 || secIndex>=count) continue;
            if (type==2) {  // STT_FUNC: actual ELF symbol address, not CCC guess.
                functionsExamined++;
                reconcileFunction(vaddr,size,name);
            } else if (type==1) { // STT_OBJECT: create missing label only; DON'T guess datatype.
                objectsExamined++;
                Address a=at(vaddr);
                if (currentProgram.getMemory().contains(a)) label(a,name);
                else unmapped++;
            }
        }

        annotate(0x80025790L,"update_mmu_cache","ELF .symtab start of Linux MMU update routine. TLBWR at +0x128 (0x800258B8); adjacent TLBWI at 0x800258BC.");
        annotate(0x800258b8L,"TLBWR #1","Exact instruction 0x42000006; one of two TLBWR opcodes in kernel .text. Correlate with interpreter guest PC.");
        annotate(0x800259e0L,"except_vec0_r5900_spr","Linux EE scratchpad refill-vector template; may be copied to a runtime vector address.");
        annotate(0x80025a08L,"TLBWR #2","Second TLBWR opcode, in delay slot. Static preparation includes EntryLo0=0x80000006, EntryLo1=0x00000006.");
        annotate(0x80025a20L,"handle_tlbl","Linux TLB load/refill handler.");
        annotate(0x80025bc0L,"handle_tlbs","Linux TLB store/refill handler; inspect page-table state for 0xC0005000.");
        annotate(0x801a48e0L,"memset","Kernel optimized memset entry, shares code with __bzero at 0x801A48FC.");
        annotate(0x801a48fcL,"__bzero","ELF STT_FUNC zero-size alias within memset. This is a LABEL, not a second overlapping function.");
        annotate(0x801a4930L,"recompiler TLB miss PC","Loop header: addiu a0,a0,0x40; recompiler reported pc=0x801A4930 while memory stores missed at 0xC0005000 onward.");
        annotate(0x801a4934L,"first loop store","sw a1,-0x40(a0); sixteen consecutive word stores per loop iteration.");
        annotate(0x8021db48L,"pgd_current","ELF .symtab size=8 in .bss. Preserve Ghidra datatype if already typed.");
        annotate(0x8021db4fL,"_end boundary","ELF _end=0x8021DB50, one byte PAST loaded memory end; don't invent an extra mapped byte for that label.");

        println("ELF SHA256="+digest+" entries="+n);
        println("EXAMINED funcs="+functionsExamined+" objects="+objectsExamined);
        println("APPLIED functionsCreated="+functionsCreated+" functionsRenamed="+functionsRenamed+
            " labelsCreated="+labelsCreated+" alreadyNamed="+already);
        println("PRESERVED overlaps="+overlaps+" invalidNames="+badNames+" unmapped="+unmapped+
            " zeroSizeFunctionAliases="+emptyAliases);
        println("Finished: type/signature declarations deliberately unchanged. Examine overlaps individually.");
    }
}
