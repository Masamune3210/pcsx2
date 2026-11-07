// Dark Cloud 2 sound-transition follow-up.
// Run directly against the already-open MODMIDI.IRX / MODHSYN.IRX Ghidra programs.
// Produces a report of the actual helper implementations behind song stop/start
// and synthesizer note-off / sound-off / controller reset.
//
//@category PS2.Audio
//@menupath Tools.PS2.Trace Dark Cloud 2 MIDI and HSYN Helpers

import ghidra.app.script.GhidraScript;
import ghidra.app.decompiler.DecompInterface;
import ghidra.app.decompiler.DecompileResults;
import ghidra.program.model.address.Address;
import ghidra.program.model.listing.Function;
import ghidra.program.model.symbol.Reference;
import ghidra.program.model.symbol.ReferenceIterator;

import java.io.File;
import java.io.FileOutputStream;
import java.io.OutputStreamWriter;
import java.io.PrintWriter;
import java.nio.charset.StandardCharsets;
import java.text.SimpleDateFormat;
import java.util.Date;
import java.util.LinkedHashMap;
import java.util.Map;
import java.util.Set;

public class TraceDC2MidiHsynState_v2 extends GhidraScript {
    private static final class Target {
        final long address;
        final String why;
        Target(long address, String why) {
            this.address = address;
            this.why = why;
        }
    }

    // Addresses from decompilations in the user's existing Ghidra reports.
    private static final Target[] MIDI = {
        new Target(0x0140, "Module initialization and callback registration"),
        new Target(0x02314, "Validate and resolve active song/MIDI instances"),
        new Target(0x02364, "Song setup/transition helper"),
        new Target(0x02C38, "Song play/stop dispatch"),
        new Target(0x018D4, "MIDI play/stop dispatch"),
        new Target(0x014CC, "Called when stopping MIDI playback"),
        new Target(0x01630, "Called when starting MIDI playback"),
        new Target(0x01580, "Per-MIDI tick dispatch"),
        new Target(0x02DE4, "Per-song tick dispatch"),
        new Target(0x019A4, "MIDI track state initialization"),
        new Target(0x00F88, "MIDI event/location progress"),
        new Target(0x02558, "Caller of MIDI selection/stop"),
        new Target(0x02860, "Caller of MIDI and song play switches")
    };

    private static final Target[] HSYN = {
        new Target(0x00050, "Module initialization and callback registration"),
        new Target(0x010B0, "Per-channel controller reset helper"),
        new Target(0x060D4, "Per-channel all-notes-off helper"),
        new Target(0x06214, "Per-channel all-sounds-off helper"),
        new Target(0x01B74, "Resolve synthesizer instance and channel group"),
        new Target(0x0173C, "Main synthesizer tick and voice control"),
        new Target(0x05C10, "Pending synthesizer event completion"),
        new Target(0x04DC0, "Synthesizer state readback"),
        new Target(0x037A4, "Synthesizer state readback"),
        new Target(0x09DDC, "SPU/libsd register read helper"),
        new Target(0x01C40, "AllNoteOff wrapper"),
        new Target(0x01CA4, "AllSoundOff wrapper"),
        new Target(0x01D08, "ResetAllControler wrapper")
    };

    private void emitFunction(PrintWriter out, DecompInterface decompiler,
            Target target) {
        Address addr = toAddr(target.address);
        Function fn = currentProgram.getFunctionManager().getFunctionAt(addr);
        out.printf("%n===================================================%n%s @ %s%n",
            target.why, addr);
        if (fn == null) {
            Function containing = currentProgram.getFunctionManager().getFunctionContaining(addr);
            out.println("No function at address. Containing function: " +
                (containing == null ? "<none>" :
                 containing.getEntryPoint() + " " + containing.getName(true)));
            return;
        }

        out.println("Function: " + fn.getName(true));
        out.println("Signature (as stored): " + fn.getPrototypeString(true, true));
        out.println("Body: " + fn.getBody());
        try {
            Set<Function> called = fn.getCalledFunctions(monitor);
            out.println("Direct callees:");
            for (Function child : called)
                out.println("  " + child.getEntryPoint() + " " + child.getName(true));
            Set<Function> calling = fn.getCallingFunctions(monitor);
            out.println("Direct callers:");
            for (Function parent : calling)
                out.println("  " + parent.getEntryPoint() + " " + parent.getName(true));
        } catch (Exception ex) {
            out.println("Call graph error: " + ex);
        }
        out.println("References to function entry:");
        ReferenceIterator refs = currentProgram.getReferenceManager()
            .getReferencesTo(fn.getEntryPoint());
        int count = 0;
        while (refs.hasNext() && count++ < 120) {
            Reference ref = refs.next();
            out.println("  " + ref.getFromAddress() + " -> " +
                ref.getToAddress() + " " + ref.getReferenceType());
        }
        Map<String, String> referencedAddresses = new LinkedHashMap<>();
        currentProgram.getListing().getInstructions(fn.getBody(), true)
            .forEachRemaining(instruction -> {
                for (Reference reference :
                        currentProgram.getReferenceManager().getReferencesFrom(instruction.getAddress())) {
                    if (!reference.getReferenceType().isFlow()) {
                        String dst = reference.getToAddress().toString();
                        referencedAddresses.putIfAbsent(dst, instruction.getAddress() +
                            " -> " + dst + " " + reference.getReferenceType());
                    }
                }
            });
        out.println("Referenced non-flow addresses:");
        for (String entry : referencedAddresses.values())
            out.println("  " + entry);

        try {
            DecompileResults result = decompiler.decompileFunction(fn, 90, monitor);
            if (result.decompileCompleted() && result.getDecompiledFunction() != null) {
                out.println("DECOMPILED C:");
                out.println(result.getDecompiledFunction().getC());
            } else {
                out.println("Decompilation unavailable: " + result.getErrorMessage());
            }
        } catch (Exception ex) {
            out.println("Decompilation error: " + ex);
        }
        out.flush();
    }

    @Override
    public void run() throws Exception {
        String program = currentProgram.getName().toUpperCase(java.util.Locale.ROOT);
        final String module;
        final Target[] targets;
        if (program.contains("MODMIDI")) {
            module = "modmidi";
            targets = MIDI;
        } else if (program.contains("MODHSYN")) {
            module = "modhsyn";
            targets = HSYN;
        } else {
            throw new IllegalArgumentException("Run this on an existing MODMIDI or MODHSYN program");
        }

        File outputDir = new File("G:\\Users\\Johnny\\Downloads\\2");
        if (!outputDir.isDirectory() && !outputDir.mkdirs())
            throw new IllegalStateException("Cannot create output directory " + outputDir);
        String stamp = new SimpleDateFormat("yyyyMMdd_HHmmss_SSS").format(new Date());
        File report = new File(outputDir, "DC2_" + module + "_helper_bodies_" + stamp + ".txt");
        DecompInterface d = new DecompInterface();
        d.openProgram(currentProgram);
        try (PrintWriter out = new PrintWriter(new OutputStreamWriter(
                    new FileOutputStream(report), StandardCharsets.UTF_8))) {
            out.println("Dark Cloud 2 MODMIDI/MODHSYN state transition helper audit");
            out.println("Program " + currentProgram.getName());
            out.println("Read-only: this script never changes symbols, signatures, or types.");
            for (Target target : targets) {
                if (monitor.isCancelled())
                    break;
                emitFunction(out, d, target);
            }
        } finally {
            d.dispose();
        }
        println("Saved: " + report.getAbsolutePath());
    }
}
