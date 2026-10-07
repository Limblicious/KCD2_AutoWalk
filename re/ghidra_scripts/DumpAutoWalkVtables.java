// DumpAutoWalkVtables.java
// @category KCD2_AutoWalk
// Prints concrete I_MagnetismController vtable targets.

import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.Address;
import ghidra.program.model.listing.Function;

public class DumpAutoWalkVtables extends GhidraScript {

    @Override
    protected void run() throws Exception {
        dump(
            0x183EAAE18L,
            "S_AutoController",
            new String[]{"destructor", "SetHoldLatched", "Tick", "GetRoadDistance"});
        dump(
            0x183C34910L,
            "S_OnPressController",
            new String[]{"destructor", "SetHoldLatched", "Tick", "GetRoadDistance"});
    }

    private void dump(long vtableVa, String className, String[] contract) throws Exception {
        println(String.format("%s vtable @ 0x%X", className, vtableVa));
        Address base = toAddr(vtableVa);

        for (int i = 0; i < contract.length; i++) {
            Address slot = base.add(i * 8L);
            long targetVa = getLong(slot);
            Address target = toAddr(targetVa);
            Function fn = getFunctionAt(target);
            String fnName = fn != null ? fn.getName() : "<no function>";

            println(String.format(
                "  [%d] %-16s slot=0x%X target=0x%X name=%s",
                i, contract[i], slot.getOffset(), targetVa, fnName));
        }
    }
}
