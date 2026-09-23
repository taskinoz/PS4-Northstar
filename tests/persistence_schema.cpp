#include "northstar_ps4/persistence_schema.h"
#include <cassert>
#include <cstdio>
#include <fstream>
#include <iterator>
using namespace northstar::ps4::mods;
int main(int argc, char** argv) {
    std::string pc = "int newPrimeTitans\nint netWorth\nint newTitanExecutions\nint unlockedTitanExecutions\nbool factionGiftsFixed\nint newCommsIcons[5]\nint unlockedCommsIcons[5]\nbool custom_emoji_initialized\nint custom_emoji[4]\nint randomFactionLevelUnlocks[faction]\nint randomPlayerLevelUnlocks\nint randomTitanLevelUnlocks[titanClasses]\nint randomWeaponLevelUnlocks[loadoutWeaponsAndAbilities]\nint randomColiseumUnlocks\n", result, error;
    std::string base = "int credits\n$STRUCT_START S\nint netWorth\n$STRUCT_END\n// int newPrimeTitans\n";
    assert(ExtendPs4PersistenceRoots(base, pc, result, error));
    assert(result.compare(0, base.size(), base) == 0);
    bool found;
    assert(PdefRootDeclaration(result, "netWorth", "int", found, error) && found);
    assert(PdefRootDeclaration(result, "newPrimeTitans", "int", found, error) && found);
    assert(PdefRootDeclaration(result, "factionGiftsFixed", "bool", found, error) && found);
    assert(!ExtendPs4PersistenceRoots("int factionGiftsFixed\n", pc, result, error));
    assert(ExtendPs4PersistenceRoots(base, pc, result, error));
    assert(!ExtendPs4PersistenceRoots("int newCommsIcons[6]\n", pc, result, error));
    assert(!ExtendPs4PersistenceRoots("int custom_emoji\n", pc, result, error));
    assert(ExtendPs4PersistenceRoots(base, pc, result, error));
    std::string again;
    assert(ExtendPs4PersistenceRoots(result, pc, again, error) && again == result);
    result = "unchanged";
    assert(!ExtendPs4PersistenceRoots("bool netWorth\n", pc, result, error) && result == "unchanged");
    assert(!ExtendPs4PersistenceRoots("int netWorth[2]\n", pc, result, error));
    assert(!ExtendPs4PersistenceRoots("$STRUCT_START Broken\n", pc, result, error));
    assert(!ExtendPs4PersistenceRoots("$ENUM_START E\n$STRUCT_END\n", pc, result, error));
    assert(!ExtendPs4PersistenceRoots("int netWorth\nint netWorth\n", pc, result, error));
    assert(!ExtendPs4PersistenceRoots(base, "int netWorth\nint newTitanExecutions\nint unlockedTitanExecutions\nbool factionGiftsFixed\nint newCommsIcons[5]\nint unlockedCommsIcons[5]\nbool custom_emoji_initialized\nint custom_emoji[4]\nint randomFactionLevelUnlocks[faction]\nint randomPlayerLevelUnlocks\nint randomTitanLevelUnlocks[titanClasses]\nint randomWeaponLevelUnlocks[loadoutWeaponsAndAbilities]\nint randomColiseumUnlocks\n", result, error));
    assert(ExtendPs4PersistenceRoots("int netWorthExtra\r\n", pc, result, error));
    const std::string enumBase = "$ENUM_START loadoutWeaponsAndAbilities\nold\nps4only\n$ENUM_END\n$ENUM_START titanPassive\nkit\n$ENUM_END\n";
    const std::string enumPc = "$ENUM_START loadoutWeaponsAndAbilities\nnew\nold\n$ENUM_END\n$ENUM_START titanPassive\nkit\nnewkit\n$ENUM_END\n";
    assert(ExtendPs4LoadoutEnums(enumBase, enumPc, result, error));
    assert(result.find("old\nps4only\n\tnew\n") != std::string::npos);
    assert(ExtendPs4LoadoutEnums(result, enumPc, again, error) && again == result);
    assert(!ExtendPs4LoadoutEnums("", enumPc, result, error));
    assert(!ExtendPs4LoadoutEnums(enumBase, "$ENUM_START loadoutWeaponsAndAbilities\na\na\n$ENUM_END\n", result, error));
    const std::string sizes = "int bits[4]\nint keep[9]\n$STRUCT_START S\nint bits\n$STRUCT_END\nint symbolic[E]\n";
    const std::string sizesPc = "int bits[5]\nint keep[2]\n$STRUCT_START S\nint bits[6]\n$STRUCT_END\nint symbolic[12]\n";
    assert(ExtendPs4IntCapacities(sizes, sizesPc, result, error));
    assert(result == "int bits[5]\nint keep[9]\n$STRUCT_START S\nint bits[6]\n$STRUCT_END\nint symbolic[E]\n");
    assert(ExtendPs4IntCapacities(result, sizesPc, again, error) && result == again);
    assert(!ExtendPs4IntCapacities("int bits\nint bits[2]\n", sizesPc, result, error));
    if (argc == 3) {
        std::ifstream a(argv[1], std::ios::binary), b(argv[2], std::ios::binary);
        assert(a && b);
        base.assign(std::istreambuf_iterator<char>(a), {}); pc.assign(std::istreambuf_iterator<char>(b), {});
        assert(ExtendPs4PersistenceRoots(base, pc, result, error));
        assert(result.compare(0, base.size(), base) == 0);
        std::string full;
        assert(ExtendPs4LoadoutEnums(result, pc, full, error));
        for (const char* name : {"loadoutWeaponsAndAbilities", "titanPassive"}) {
            std::vector<std::string> oldValues, newValues;
            std::size_t end;
            assert(PdefEnum(base, name, oldValues, end, error));
            assert(PdefEnum(full, name, newValues, end, error));
            assert(newValues.size() >= oldValues.size());
            for (std::size_t i = 0; i < oldValues.size(); ++i) assert(oldValues[i] == newValues[i]);
        }
        assert(ExtendPs4LoadoutEnums(full, pc, again, error) && again == full);
        assert(ExtendPs4IntCapacities(full, pc, result, error));
        assert(ExtendPs4IntCapacities(result, pc, again, error) && again == result);
        full = result;
        std::printf("Live persistence schema: %zu -> %zu bytes; root prefix and existing enum indices preserved.\n", base.size(), full.size());
    }
    std::puts("Persistence schema tests passed.");
}
