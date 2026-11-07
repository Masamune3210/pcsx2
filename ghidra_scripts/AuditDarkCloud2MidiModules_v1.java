// Resolve proven Sony IOP exports and capture the Dark Cloud 2 MIDI transition paths.
// Import MODMIDI.IRX.gzf or MODHSYN.IRX.gzf, then run this script on the open program.
// The script intentionally preserves existing unresolved function signatures and types.
// Reports are written to G:\Users\Johnny\Downloads\2.
//
//@category PS2.Audio
//@menupath Tools.PS2.Audit Dark Cloud 2 MIDI Modules

import ghidra.app.script.GhidraScript;
import ghidra.app.decompiler.DecompInterface;
import ghidra.app.decompiler.DecompileResults;
import ghidra.program.model.address.Address;
import ghidra.program.model.listing.Function;
import ghidra.program.model.listing.Instruction;
import ghidra.program.model.listing.InstructionIterator;
import ghidra.program.model.symbol.Reference;
import ghidra.program.model.symbol.ReferenceIterator;
import ghidra.program.model.symbol.SourceType;
import ghidra.program.model.symbol.Symbol;
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

public class AuditDarkCloud2MidiModules_v1 extends GhidraScript {
    private static class Export {
        final int ordinal;
        final long address;
        final String name;
        Export(int ordinal, long address, String name) {
            this.ordinal = ordinal;
            this.address = address;
            this.name = name;
        }
    }

    // Explicit ordinal-to-address pairs extracted from the supplied GZF databases.
    // Do not synthesize ordinal 25 for MODMIDI or ordinal 28 for MODHSYN:
    // those exports were not observed in the supplied archives.
    private static final Export[] MODMIDI = {
        new Export(4, 0x140L, "sceMidi_Init"),
        new Export(5, 0x560L, "sceMidi_ATick"),
        new Export(6, 0x3a8L, "sceMidi_Load"),
        new Export(7, 0x239cL, "sceMidi_SelectSong"),
        new Export(8, 0x2c38L, "sceMidi_SongPlaySwitch"),
        new Export(9, 0x2ed0L, "sceMidi_SongSetVolume"),
        new Export(10, 0x2f54L, "sceMidi_SongVolumeChange"),
        new Export(11, 0x2fd8L, "sceMidi_SongSetAbsoluteTempo"),
        new Export(12, 0x3064L, "sceMidi_SongSetRelativeTempo"),
        new Export(13, 0x327cL, "sceMidi_SongSetLocation"),
        new Export(14, 0x1a5cL, "sceMidi_SelectMidi"),
        new Export(15, 0x18d4L, "sceMidi_MidiPlaySwitch"),
        new Export(16, 0x1c00L, "sceMidi_MidiSetLocation"),
        new Export(17, 0x1d74L, "sceMidi_MidiSetVolume"),
        new Export(18, 0x1e64L, "sceMidi_MidiVolumeChange"),
        new Export(19, 0x1f78L, "sceMidi_MidiSetAbsoluteTempo"),
        new Export(20, 0x1fdcL, "sceMidi_MidiGetAbsoluteTempo"),
        new Export(21, 0x2078L, "sceMidi_MidiSetRelativeTempo"),
        new Export(22, 0x20c4L, "sceMidi_MidiGetRelativeTempo"),
        new Export(23, 0x2114L, "sceMidi_MidiSetUSecTempo"),
        new Export(24, 0x2158L, "sceMidi_MidiGetUSecTempo")
    };
    private static final Export[] MODHSYN = {
        new Export(4, 0x50L, "sceHSyn_Init"),
        new Export(5, 0x182cL, "sceHSyn_ATick"),
        new Export(6, 0x47cL, "sceHSyn_Load"),
        new Export(7, 0x36cL, "sceHSyn_VoiceTrans"),
        new Export(8, 0x19f8L, "sceHSyn_SetReservVoice"),
        new Export(9, 0x1d6cL, "sceHSyn_SetEffectAttr"),
        new Export(10, 0x1bc4L, "sceHSyn_SetVolume"),
        new Export(11, 0x1c08L, "sceHSyn_GetVolume"),
        new Export(12, 0x1c40L, "sceHSyn_AllNoteOff"),
        new Export(13, 0x1ca4L, "sceHSyn_AllSoundOff"),
        new Export(14, 0x1d08L, "sceHSyn_ResetAllControler"),
        new Export(15, 0x1e50L, "sceHSyn_SetVoiceStatBuffer"),
        new Export(16, 0x9b30L, "sceHSyn_SetDebugInfoBuffer"),
        new Export(17, 0x1fa8L, "sceHSyn_GetChStat"),
        new Export(18, 0x2044L, "sceHSyn_SetOutputMode"),
        new Export(19, 0x2f34L, "sceHSyn_SESetMaxVoices"),
        new Export(20, 0x2f5cL, "sceHSyn_SEAllNoteOff"),
        new Export(21, 0x2fb4L, "sceHSyn_SEAllSoundOff"),
        new Export(22, 0x300cL, "sceHSyn_SERetrieveVoiceNumberByID"),
        new Export(23, 0x323cL, "sceHSyn_MSGetVoiceStateByID"),
        new Export(24, 0x3390L, "sceHSyn_MSGetVoiceEnvelopeByID"),
        new Export(25, 0x3120L, "sceHSyn_SERetrieveAllSEMsgIDs"),
        new Export(26, 0x1b4cL, "sceHSyn_GetReservVoice"),
        new Export(27, 0x230L, "sceHSyn_GetOutputMode")
    };

    private static boolean isTransitionTarget(String name) {
        return name.equals("sceMidi_ATick") ||
            name.equals("sceMidi_Load") ||
            name.equals("sceMidi_SelectSong") ||
            name.equals("sceMidi_SongPlaySwitch") ||
            name.equals("sceMidi_SelectMidi") ||
            name.equals("sceMidi_MidiPlaySwitch") ||
            name.equals("sceMidi_MidiSetLocation") ||
            name.equals("sceHSyn_ATick") ||
            name.equals("sceHSyn_VoiceTrans") ||
            name.equals("sceHSyn_AllNoteOff") ||
            name.equals("sceHSyn_AllSoundOff") ||
            name.equals("sceHSyn_ResetAllControler") ||
            name.equals("sceHSyn_GetChStat");
    }

    private boolean hasExpectedOrdinal(Address addr, String module, Export e) {
        final String expected = "IOP_Export_" + module.toLowerCase() + "_ord_" +
            e.ordinal + "_" + Long.toHexString(e.address);
        for (Symbol symbol : currentProgram.getSymbolTable().getSymbols(addr)) {
            if (symbol.getName().equals(expected) || symbol.getName().equals(e.name))
                return true;
        }
        return false;
    }

    private void reportCalls(PrintWriter out, Function function) {
        try {
            Set<Function> called = function.getCalledFunctions(monitor);
            out.println("  Called functions:");
            for (Function target : called)
                out.println("    " + target.getEntryPoint() + " " + target.getName(true));
            Set<Function> callers = function.getCallingFunctions(monitor);
            out.println("  Calling functions:");
            for (Function source : callers)
                out.println("    " + source.getEntryPoint() + " " + source.getName(true));
            ReferenceIterator refs = currentProgram.getReferenceManager().getReferencesTo(function.getEntryPoint());
            out.println("  Direct references to entry:");
            int count = 0;
            while (refs.hasNext() && count++ < 100) {
                Reference ref = refs.next();
                out.println("    " + ref.getFromAddress() + " => " + ref.getReferenceType());
            }
        } catch (Exception ex) {
            out.println("  Reference scan failed: " + ex);
        }
    }

    private void reportDataReferences(PrintWriter out, Function function) {
        out.println("  Outgoing non-flow references (up to 120 unique targets):");
        Map<String, String> targets = new LinkedHashMap<>();
        InstructionIterator instructions = currentProgram.getListing().getInstructions(function.getBody(), true);
        while (instructions.hasNext() && targets.size() < 120 && !monitor.isCancelled()) {
            Instruction ins = instructions.next();
            for (Reference ref : currentProgram.getReferenceManager().getReferencesFrom(ins.getAddress())) {
                if (ref.getReferenceType().isFlow())
                    continue;
                String target = ref.getToAddress().toString();
                if (!targets.containsKey(target))
                    targets.put(target, ins.getAddress() + " -> " + target + " " + ref.getReferenceType());
            }
        }
        for (String value : targets.values())
            out.println("    " + value);
    }

    @Override
    public void run() throws Exception {
        if (currentProgram == null)
            throw new IllegalStateException("Open a MODMIDI or MODHSYN Ghidra program.");
        String programName = currentProgram.getName().toUpperCase(java.util.Locale.ROOT);
        final String module;
        final Export[] exports;
        if (programName.contains("MODMIDI")) {
            module = "modmidi";
            exports = MODMIDI;
        } else if (programName.contains("MODHSYN")) {
            module = "modhsyn";
            exports = MODHSYN;
        } else {
            throw new IllegalStateException("Wrong program: " + currentProgram.getName());
        }
        File outputDirectory = new File("G:\\Users\\Johnny\\Downloads\\2");
        if (!outputDirectory.isDirectory() && !outputDirectory.mkdirs())
            throw new IllegalStateException("Cannot create report directory: " + outputDirectory);

        String timestamp = new SimpleDateFormat("yyyyMMdd_HHmmss_SSS").format(new Date());
        File output = new File(outputDirectory, "DC2_" + module + "_transitions_" + timestamp + ".txt");
        DecompInterface decompiler = new DecompInterface();
        decompiler.openProgram(currentProgram);
        int renamed = 0, absent = 0, decompiled = 0;
        try (PrintWriter out = new PrintWriter(new OutputStreamWriter(
                new FileOutputStream(output), StandardCharsets.UTF_8))) {
            out.println("Dark Cloud 2 guest sound module audit");
            out.println("Program: " + currentProgram.getName());
            out.println("Language: " + currentProgram.getLanguage().getLanguageID());
            out.println("Image base: " + currentProgram.getImageBase());
            out.println("No types or parameter names were guessed by this script.");
            out.println();
            for (Export e : exports) {
                if (monitor.isCancelled())
                    break;
                Address address = toAddr(e.address);
                if (!hasExpectedOrdinal(address, module, e)) {
                    out.printf("SKIP: no matching archived export at %s ordinal %d (%s)%n",
                        address, e.ordinal, e.name);
                    absent++;
                    continue;
                }
                Function function = currentProgram.getFunctionManager().getFunctionAt(address);
                try {
                    if (function == null && currentProgram.getListing().getInstructionAt(address) != null)
                        function = createFunction(address, e.name);
                    if (function != null && (function.getName().startsWith("FUN_") ||
                            function.getName().startsWith("IOP_Export_"))) {
                        function.setName(e.name, SourceType.USER_DEFINED);
                        renamed++;
                    } else if (function == null) {
                        for (Symbol symbol : currentProgram.getSymbolTable().getSymbols(address)) {
                            if (symbol.getName().startsWith("IOP_Export_" + module + "_ord_")) {
                                symbol.setName(e.name, SourceType.USER_DEFINED);
                                renamed++;
                                break;
                            }
                        }
                    }
                } catch (Exception ex) {
                    out.printf("RENAME ERROR %s @ %s: %s%n", e.name, address, ex);
                }

                out.printf("%n=== ordinal %d: %s @ %s ===%n", e.ordinal, e.name, address);
                if (function == null) {
                    out.println("No function defined at this address. Existing symbol retained/refined.");
                    continue;
                }
                out.println("Signature (as currently known, not inferred): " +
                    function.getPrototypeString(true, true));
                if (!isTransitionTarget(e.name))
                    continue;
                reportCalls(out, function);
                reportDataReferences(out, function);
                try {
                    DecompileResults result = decompiler.decompileFunction(function, 60, monitor);
                    if (result.decompileCompleted() && result.getDecompiledFunction() != null) {
                        out.println("--- Decompiled C ---");
                        out.println(result.getDecompiledFunction().getC());
                        decompiled++;
                    } else {
                        out.println("Decompilation unavailable: " + result.getErrorMessage());
                    }
                } catch (Exception ex) {
                    out.println("Decompilation error: " + ex);
                }
                out.flush();
            }
            out.printf("%nSummary: renamed=%d missing-archive-exports=%d decompiled-targets=%d%n",
                renamed, absent, decompiled);
        } finally {
            decompiler.dispose();
        }
        println("DC2 " + module + " report: " + output.getAbsolutePath());
        println("Renamed " + renamed + "; missing " + absent + "; decompiled " + decompiled);
    }
}
