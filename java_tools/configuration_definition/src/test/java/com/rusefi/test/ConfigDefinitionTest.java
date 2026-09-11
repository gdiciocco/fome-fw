package com.rusefi.test;

import com.rusefi.EnumsReader;
import com.rusefi.VariableRegistry;
import com.rusefi.newparse.ParseState;
import com.rusefi.trigger.TriggerWheelTSLogic;
import org.junit.Test;

import java.io.File;
import java.io.FileReader;
import java.io.IOException;

import static org.junit.Assert.assertNotNull;
import static org.junit.Assert.assertTrue;

public class ConfigDefinitionTest {
    private static final String FIRMWARE = "../../firmware";

    @Test
    public void testEnumIntoType() throws IOException {
        EnumsReader enumsReader = new EnumsReader();
        enumsReader.read(new FileReader(FIRMWARE + File.separator + "controllers/algo/engine_types.h"));

        VariableRegistry variableRegistry = new VariableRegistry();

        variableRegistry.readPrependValues(FIRMWARE + File.separator + "integration/fome_config.txt");


        String sb = variableRegistry.getEnumOptionsForTunerStudio(enumsReader, "engine_type_e");

        System.out.println(sb);
        assertNotNull(sb);
        assertTrue("Seems too long" + sb, sb.length() < 100000);
    }

    @Test
    public void triggerTsDefinitionsAreAvailableToNewParser() {
        ParseState parseState = new ParseState();

        new TriggerWheelTSLogic().execute("../../unit_tests", new VariableRegistry(), parseState);

        assertNotNull(parseState.findDefinition("TRIGGER_TYPE_WITHOUT_KNOWN_LOCATION"));
        assertNotNull(parseState.findDefinition("TRIGGER_TYPE_WITH_SECOND_WHEEL"));
        assertNotNull(parseState.findDefinition("TRIGGER_CRANK_BASED"));
    }
}
