#include <catch_main.hpp>

#include <boost/filesystem.hpp>
#include <boost/nowide/fstream.hpp>

#include "libslic3r/PresetBundle.hpp"

using namespace Slic3r;

// Vendor bundle mirroring the structure that broke nozzle-size switching:
// two printer variants, a Rigid print profile per variant (different layer heights),
// an Elastic print profile only for the 0.6 variant with the same layer height as
// the 0.4 Rigid profile, and a TPU filament tied to the Elastic print profile.
static std::string test_bundle_ini(bool with_alias)
{
    std::string alias_04 = with_alias ? "alias = Rigid\n" : "";
    std::string alias_06 = with_alias ? "alias = Rigid\n" : "";
    return
        "[vendor]\n"
        "name = TestVendor\n"
        "config_version = 1.0.0\n"
        "\n"
        "[printer_model:TV300]\n"
        "name = TV300\n"
        "variants = 0.4; 0.6\n"
        "technology = FFF\n"
        "family = TV\n"
        "default_materials = PLA @TV\n"
        "\n"
        "[printer:TV 0.4]\n"
        "printer_technology = FFF\n"
        "printer_model = TV300\n"
        "printer_variant = 0.4\n"
        "nozzle_diameter = 0.4\n"
        "default_print_profile = Rigid 0.4\n"
        "default_filament_profile = PLA @TV\n"
        "\n"
        "[printer:TV 0.6]\n"
        "printer_technology = FFF\n"
        "printer_model = TV300\n"
        "printer_variant = 0.6\n"
        "nozzle_diameter = 0.6\n"
        "default_print_profile = Rigid 0.6\n"
        "default_filament_profile = PLA @TV\n"
        "\n"
        // "Elastic 0.6" sorts before both Rigid profiles and shares its layer
        // height with "Rigid 0.4", so a naive first-compatible / layer-height
        // match picks it over the declared default_print_profile.
        "[print:Elastic 0.6]\n"
        "layer_height = 0.2\n"
        "compatible_printers = TV 0.6\n"
        "\n"
        "[print:Rigid 0.4]\n"
        + alias_04 +
        "layer_height = 0.2\n"
        "compatible_printers = TV 0.4\n"
        "\n"
        "[print:Rigid 0.6]\n"
        + alias_06 +
        "layer_height = 0.3\n"
        "compatible_printers = TV 0.6\n"
        "\n"
        "[filament:PLA @TV]\n"
        "filament_type = PLA\n"
        "compatible_printers = TV 0.4; TV 0.6\n"
        "compatible_prints = Rigid 0.4; Rigid 0.6\n"
        "\n"
        "[filament:TPU @TV]\n"
        "filament_type = FLEX\n"
        "compatible_printers = TV 0.6\n"
        "compatible_prints = Elastic 0.6\n";
}

static boost::filesystem::path write_test_bundle(bool with_alias)
{
    boost::filesystem::path path = boost::filesystem::temp_directory_path() /
        boost::filesystem::unique_path("slic3r_test_bundle_%%%%%%%%.ini");
    boost::nowide::ofstream out(path.string());
    out << test_bundle_ini(with_alias);
    out.close();
    return path;
}

static PresetBundle load_test_bundle(bool with_alias)
{
    boost::filesystem::path path = write_test_bundle(with_alias);
    PresetBundle bundle;
    auto [substitutions, presets_loaded] = bundle.load_configbundle(
        path.string(), PresetBundle::LoadConfigBundleAttribute::LoadSystem, ForwardCompatibilitySubstitutionRule::Disable);
    boost::filesystem::remove(path);
    REQUIRE(presets_loaded > 0);
    return bundle;
}

// Select printer / print / filament and refresh the compatibility flags,
// approximating the state after the GUI activated these presets.
static void select_presets(PresetBundle &bundle, const std::string &printer, const std::string &print, const std::string &filament)
{
    bundle.printers.select_preset_by_name(printer, true);
    bundle.update_multi_material_filament_presets();
    bundle.fff_prints.select_preset_by_name(print, true);
    bundle.filaments.select_preset_by_name(filament, true);
    bundle.set_filament_preset(0, filament);
    bundle.update_compatible(PresetSelectCompatibleType::Never);
}

// Approximates Tab::select_preset() switching the printer preset.
static void switch_printer(PresetBundle &bundle, const std::string &printer)
{
    bundle.printers.select_preset_by_name(printer, true);
    bundle.update_compatible(PresetSelectCompatibleType::Always);
}

TEST_CASE("Switching nozzle variant follows the print profile alias", "[PresetBundle]") {
    PresetBundle bundle = load_test_bundle(true);
    select_presets(bundle, "TV 0.4", "Rigid 0.4", "PLA @TV");
    REQUIRE(bundle.fff_prints.get_selected_preset().name == "Rigid 0.4");
    REQUIRE(bundle.extruders_filaments[0].get_selected_preset()->name == "PLA @TV");

    switch_printer(bundle, "TV 0.6");
    CHECK(bundle.fff_prints.get_selected_preset().name == "Rigid 0.6");
    CHECK(bundle.extruders_filaments[0].get_selected_preset()->name == "PLA @TV");

    switch_printer(bundle, "TV 0.4");
    CHECK(bundle.fff_prints.get_selected_preset().name == "Rigid 0.4");
    CHECK(bundle.extruders_filaments[0].get_selected_preset()->name == "PLA @TV");
}

TEST_CASE("default_print_profile outranks a layer height match", "[PresetBundle]") {
    // No aliases here: the printer's default_print_profile alone must beat
    // "Elastic 0.6", which matches the old profile's layer height.
    PresetBundle bundle = load_test_bundle(false);
    select_presets(bundle, "TV 0.4", "Rigid 0.4", "PLA @TV");

    switch_printer(bundle, "TV 0.6");
    CHECK(bundle.fff_prints.get_selected_preset().name == "Rigid 0.6");
    CHECK(bundle.extruders_filaments[0].get_selected_preset()->name == "PLA @TV");
}

TEST_CASE("Incompatible filament falls back to default_filament_profile", "[PresetBundle]") {
    PresetBundle bundle = load_test_bundle(true);
    select_presets(bundle, "TV 0.6", "Elastic 0.6", "TPU @TV");
    REQUIRE(bundle.extruders_filaments[0].get_selected_preset()->name == "TPU @TV");

    switch_printer(bundle, "TV 0.4");
    CHECK(bundle.fff_prints.get_selected_preset().name == "Rigid 0.4");
    CHECK(bundle.extruders_filaments[0].get_selected_preset()->name == "PLA @TV");
}
