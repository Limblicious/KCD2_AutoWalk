// ApplyAutoWalkSeeds.java
// @category KCD2_AutoWalk
// Applies known build-15693 labels and resolves controller vtable targets.
// Safe: modifies only the Ghidra project database.

import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.Address;
import ghidra.program.model.listing.Function;
import ghidra.program.model.symbol.SourceType;

public class ApplyAutoWalkSeeds extends GhidraScript {

    private static final long EXPECTED_IMAGE_BASE = 0x180000000L;

    private static final Object[][] FUNCTION_SEEDS = {
        {0x180A4E5ACL, "S_HorseRoadFollow_Tick"},
        {0x180A4E9B8L, "HorseRoadFollow_RebuildControllerMode"},
        {0x181ED8860L, "HorseRoadSample_Wrapper"},
        {0x181ED7ED0L, "HorseRoadSample_StateBuilder"},
        {0x180A0A124L, "HorseRoadSample_Sampler"},
        {0x1829FC188L, "S_AutoController_Factory"},
        {0x181932168L, "S_OnPressController_Factory"},
        {0x1819321F8L, "S_OnPressController_Ctor"},
        {0x18059B800L, "HorseYaw_SmoothCD"},
        {0x1806CEB68L, "S_HorseData_Update"},
        {0x1839C3748L, "C_CameraRider_Compose"},
        {0x18094D030L, "C_CameraFirstPerson_Compose"},
        {0x1806CCAF8L, "HorseFlatYaw_To_RiderLookAccum"},
        {0x180A501E8L, "MountedCamera_Centering"},
        {0x1804415E0L, "C_ActorPhysicsState_Tick"},
        {0x18053B508L, "ActorViewLimit_Clamp"}
    };

    private static final Object[][] LABEL_SEEDS = {
        {0x180A4E98CL, "I_MagnetismController_SetHoldLatched_Dispatch"},
        {0x180A4E768L, "I_MagnetismController_Tick_Dispatch"},
        {0x180A4FDF1L, "HorseYaw_SmoothingUpdate_Region"},
        {0x180A09944L, "RoadChooser_ForkCandidateFilter_Callsite"},
        {0x180A09D7EL, "RoadChooser_InitialSnapFilter_Callsite"},
        {0x183EAAE18L, "VTABLE_S_AutoController"},
        {0x183C34910L, "VTABLE_S_OnPressController_I_MagnetismController"},
        {0x183C348D0L, "VTABLE_S_OnPressController_I_RiderPlayerStateMachineModifier"},
        {0x184C176B8L, "RTTI_S_AutoController"},
        {0x184C176F0L, "RTTI_S_OnPressController"},
        {0x18504EEE0L, "RTTI_C_CameraRider"}
    };

    @Override
    protected void run() throws Exception {
        long imageBase = currentProgram.getImageBase().getOffset();
        if (imageBase != EXPECTED_IMAGE_BASE) {
            throw new IllegalStateException(
                String.format("Unexpected image base 0x%X; expected 0x%X. Refusing absolute seed application.",
                    imageBase, EXPECTED_IMAGE_BASE));
        }

        println("[AutoWalk] Applying function seeds...");
        for (Object[] seed : FUNCTION_SEEDS) {
            applyFunctionSeed((Long) seed[0], (String) seed[1]);
        }

        println("[AutoWalk] Applying label/vtable/RTTI seeds...");
        for (Object[] seed : LABEL_SEEDS) {
            applyLabel((Long) seed[0], (String) seed[1]);
        }

        println("[AutoWalk] Resolving controller vtables...");
        resolveVtable(
            0x183EAAE18L,
            "S_AutoController",
            new String[]{"destructor", "SetHoldLatched", "Tick", "GetRoadDistance"});
        resolveVtable(
            0x183C34910L,
            "S_OnPressController",
            new String[]{"destructor", "SetHoldLatched", "Tick", "GetRoadDistance"});

        println("[AutoWalk] Seed application complete.");
    }

    private void applyFunctionSeed(long va, String name) throws Exception {
        Address addr = toAddr(va);
        Function fn = getFunctionAt(addr);
        if (fn == null) {
            disassemble(addr);
            fn = createFunction(addr, name);
        }

        if (fn != null) {
            fn.setName(name, SourceType.USER_DEFINED);
            println(String.format("  function 0x%X -> %s", va, name));
        }
        else {
            applyLabel(va, name);
            println(String.format("  label-only 0x%X -> %s", va, name));
        }
    }

    private void applyLabel(long va, String name) throws Exception {
        createLabel(toAddr(va), name, true);
    }

    private void resolveVtable(long vtableVa, String className, String[] slots) throws Exception {
        Address base = toAddr(vtableVa);

        for (int i = 0; i < slots.length; i++) {
            Address slotAddr = base.add(i * 8L);
            long targetVa = getLong(slotAddr);
            Address target = toAddr(targetVa);
            String desired = className + "_" + slots[i];

            Function fn = getFunctionAt(target);
            if (fn == null) {
                disassemble(target);
                fn = createFunction(target, desired);
            }

            if (fn != null) {
                fn.setName(desired, SourceType.USER_DEFINED);
            }
            else {
                createLabel(target, desired, true);
            }

            println(String.format(
                "  %s[%d] slot=0x%X target=0x%X -> %s",
                className, i, slotAddr.getOffset(), targetVa, desired));
        }
    }
}
