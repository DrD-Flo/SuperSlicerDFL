#ifndef slic3r_GCode_hpp_
#define slic3r_GCode_hpp_

#include "libslic3r.h"
#include "EdgeGrid.hpp"
#include "ExPolygon.hpp"
#include "GCodeWriter.hpp"
#include "Layer.hpp"
#include "Point.hpp"
#include "Print.hpp"
#include "PlaceholderParser.hpp"
#include "PrintConfig.hpp"
#include "GCode/AvoidCrossingPerimeters.hpp"
#include "GCode/CoolingBuffer.hpp"
#include "GCode/FanMover.hpp"
#include "GCode/FindReplace.hpp"
#include "GCode/SpiralVase.hpp"
#include "GCode/ToolOrdering.hpp"
#include "GCode/WipeTower.hpp"
#include "GCode/SeamPlacer.hpp"
#include "GCode/GCodeProcessor.hpp"
#include "GCode/ThumbnailData.hpp"

#include <memory>
#include <map>
#include <string>
#include <chrono>

#include "GCode/PressureEqualizer.hpp"

namespace Slic3r {

// Forward declarations.
class GCode;

namespace { struct Item; }
struct PrintInstance;
class ConstPrintObjectPtrsAdaptor;

class OozePrevention {
public:
    bool enable;
    Points standby_points;
    
    OozePrevention() : enable(false) {}
    std::string pre_toolchange(GCode &gcodegen);
    std::string post_toolchange(GCode &gcodegen);
    
private:
    int _get_temp(GCode &gcodegen);
};

class Wipe {
public:
    bool enable;
    Polyline path;
    
    Wipe() : enable(false) {}
    bool has_path() const { return !this->path.points.empty(); }
    void reset_path() { this->path = Polyline(); }
    std::string wipe(GCode &gcodegen, bool toolchange = false);
};

class WipeTowerIntegration {
public:
    WipeTowerIntegration(
        const PrintConfig                                           &print_config,
        const std::vector<WipeTower::ToolChangeResult>              &priming,
        const std::vector<std::vector<WipeTower::ToolChangeResult>> &tool_changes,
        const WipeTower::ToolChangeResult                           &final_purge) :
        m_left(/*float(print_config.wipe_tower_x.value)*/ 0.f),
        m_right(float(/*print_config.wipe_tower_x.value +*/ print_config.wipe_tower_width.value)),
        m_wipe_tower_pos(float(print_config.wipe_tower_x.value), float(print_config.wipe_tower_y.value)),
        m_wipe_tower_rotation(float(print_config.wipe_tower_rotation_angle)),
        m_extruder_offsets(print_config.extruder_offset.values),
        m_priming(priming),
        m_tool_changes(tool_changes),
        m_final_purge(final_purge),
        m_layer_idx(-1),
        m_tool_change_idx(0)
    {}

    std::string prime(GCode &gcodegen);
    void next_layer() { ++ m_layer_idx; m_tool_change_idx = 0; }
    std::string tool_change(GCode &gcodegen, int extruder_id, bool finish_layer);
    std::string finalize(GCode &gcodegen);
    std::vector<float> used_filament_length() const;

private:
    WipeTowerIntegration& operator=(const WipeTowerIntegration&);
    std::string append_tcr(GCode &gcodegen, const WipeTower::ToolChangeResult &tcr, int new_extruder_id, double z = -1.) const;

    // Postprocesses gcode: rotates and moves G1 extrusions and returns result
    std::string post_process_wipe_tower_moves(const WipeTower::ToolChangeResult& tcr, const Vec2f& translation, float angle) const;

    // Left / right edges of the wipe tower, for the planning of wipe moves.
    const float                                                  m_left;
    const float                                                  m_right;
    const Vec2f                                                  m_wipe_tower_pos;
    const float                                                  m_wipe_tower_rotation;
    const std::vector<Vec2d>                                     m_extruder_offsets;

    // Reference to cached values at the Printer class.
    const std::vector<WipeTower::ToolChangeResult>              &m_priming;
    const std::vector<std::vector<WipeTower::ToolChangeResult>> &m_tool_changes;
    const WipeTower::ToolChangeResult                           &m_final_purge;
    // Current layer index.
    int                                                          m_layer_idx;
    int                                                          m_tool_change_idx;
    double                                                       m_last_wipe_tower_print_z = 0.f;
};

class ColorPrintColors
{
    static const std::vector<std::string> Colors;
public:
    static const std::vector<std::string>& get() { return Colors; }
};

struct LayerResult {
    std::string gcode;
    size_t      layer_id;
    // Is spiral vase post processing enabled for this layer?
    bool        spiral_vase_enable { false };
    // Should the cooling buffer content be flushed at the end of this layer?
    bool        cooling_buffer_flush { false };
    // Is indicating if this LayerResult should be processed, or it is just inserted artificial LayerResult.
    // It is used for the pressure equalizer because it needs to buffer one layer back.
    bool        nop_layer_result { false };

    static LayerResult make_nop_layer_result() { return {"", std::numeric_limits<coord_t>::max(), false, false, true}; }
};

class GCode : ExtrusionVisitorConst  {
public:        
    GCode() : 
    	m_origin(Vec2d::Zero()),
        m_enable_loop_clipping(true), 
        m_enable_cooling_markers(false), 
        m_enable_extrusion_role_markers(false), 
        m_last_processor_extrusion_role(erNone),
        m_layer_count(0),
        m_layer_index(-1), 
        m_layer(nullptr),
        m_object_layer_over_raft(false),
        m_volumetric_speed(0),
        m_last_pos_defined(false),
        m_last_extrusion_role(erNone),
        m_last_width(0.0f),
#if ENABLE_GCODE_VIEWER_DATA_CHECKING
        m_last_mm3_per_mm(0.0),
#endif // ENABLE_GCODE_VIEWER_DATA_CHECKING
        m_brim_done(false),
        m_second_layer_things_done(false),
        m_silent_time_estimator_enabled(false),
        m_last_obj_copy(nullptr, Point(std::numeric_limits<coord_t>::max(), std::numeric_limits<coord_t>::max())),
        m_last_too_small(ExtrusionRole::erNone)
    {
        cooldown_marker_init();
    }
    ~GCode() = default;

    // throws std::runtime_exception on error,
    // throws CanceledException through print->throw_if_canceled().
    void            do_export(Print* print, const char* path, GCodeProcessorResult* result = nullptr, ThumbnailsGeneratorCallback thumbnail_cb = nullptr);

    // Exported for the helper classes (OozePrevention, Wipe) and for the Perl binding for unit tests.
    const Vec2d&    origin() const { return m_origin; }
    void            set_origin(const Vec2d &pointf);
    void            set_origin(const coordf_t x, const coordf_t y) { this->set_origin(Vec2d(x, y)); }
    const Point&    last_pos() const { return m_last_pos; }
    Vec2d           point_to_gcode(const Point &point) const;
    Vec3d           point_to_gcode(const Point &point, coord_t z_offset) const;
    Point           gcode_to_point(const Vec2d &point) const;
    const FullPrintConfig &config() const { return m_config; }
    const Layer*    layer() const { return m_layer; }
    GCodeWriter&    writer() { return m_writer; }
    const GCodeWriter& writer() const { return m_writer; }
    PlaceholderParser& placeholder_parser() { return m_placeholder_parser; }
    const PlaceholderParser& placeholder_parser() const { return m_placeholder_parser; }
    // Process a template through the placeholder parser, collect error messages to be reported
    // inside the generated string and after the G-code export finishes.
    std::string     placeholder_parser_process(const std::string &name, const std::string &templ, uint16_t current_extruder_id, const DynamicConfig *config_override = nullptr);
    bool            enable_cooling_markers() const { return m_enable_cooling_markers; }
    std::string     extrusion_role_to_string_for_parser(const ExtrusionRole &);

    // For Perl bindings, to be used exclusively by unit tests.
    unsigned int    layer_count() const { return m_layer_count; }
    void            set_layer_count(unsigned int value) { m_layer_count = value; }
    void            apply_print_configs(const Print &print);

    // append full config to the given string
    static void append_full_config(const Print& print, std::string& str);

    // Object and support extrusions of the same PrintObject at the same print_z.
    // public, so that it could be accessed by free helper functions from GCode.cpp
    struct LayerToPrint
    {
        LayerToPrint() : object_layer(nullptr), support_layer(nullptr) {}
        const Layer* 		object_layer;
        const SupportLayer* support_layer;
        const Layer* 		layer()   const { return (object_layer != nullptr) ? object_layer : support_layer; }
        const PrintObject* 	object()  const { return (this->layer() != nullptr) ? this->layer()->object() : nullptr; }
        coordf_t            print_z() const { return (object_layer != nullptr && support_layer != nullptr) ? 0.5 * (object_layer->print_z + support_layer->print_z) : this->layer()->print_z; }
    };

private:
    class GCodeOutputStream {
    public:
        GCodeOutputStream(FILE* f, GCodeProcessor& processor, GCode& gcodegen) : f(f), m_processor(processor), m_gcodegen(gcodegen) {}
        ~GCodeOutputStream() { this->close(); }

        // Set a find-replace post-processor to modify the G-code before GCodePostProcessor.
        // It is being set to null inside process_layers(), because the find-replace process
        // is being called on a secondary thread to improve performance.
        void set_find_replace(GCodeFindReplace *find_replace, bool enabled) { m_find_replace_backup = find_replace; m_find_replace = enabled ? find_replace : nullptr; }
        void set_only_ascii(bool only_ascii) { m_only_ascii = only_ascii; }
        void find_replace_enable() { m_find_replace = m_find_replace_backup; }
        void find_replace_supress() { m_find_replace = nullptr; }

        bool is_open() const { return f; }
        bool is_error() const;
        
        void flush();
        void close();

        // Write a string into a file.
        void write(const std::string& what) { this->write(what.c_str()); }
        void write(const char* what);

        // Write a string into a file. 
        // Add a newline, if the string does not end with a newline already.
        // Used to export a custom G-code section processed by the PlaceholderParser.
        void writeln(const std::string& what);

        // Formats and write into a file the given data. 
        void write_format(const char* format, ...);

    private:
        FILE             *f { nullptr };
        // Find-replace post-processor to be called before GCodePostProcessor.
        GCodeFindReplace *m_find_replace { nullptr };
        bool              m_only_ascii;
        // If suppressed, the backoup holds m_find_replace.
        GCodeFindReplace *m_find_replace_backup { nullptr };
        GCodeProcessor   &m_processor;
        GCode            &m_gcodegen;
    };
    void            _do_export(Print &print, GCodeOutputStream &file, ThumbnailsGeneratorCallback thumbnail_cb);
    void            _move_to_print_object(std::string& gcode_out, const Print& print, size_t finished_objects, uint16_t initial_extruder_id);
    void            _init_multiextruders(const Print& print, std::string& gcode_out, GCodeWriter& writer, const ToolOrdering& tool_ordering, const std::string& custom_gcode);

    static std::vector<LayerToPrint>                                   collect_layers_to_print(const PrintObject &object, Print::StatusMonitor &status_monitor);
    static std::vector<std::pair<coordf_t, std::vector<LayerToPrint>>> collect_layers_to_print(const Print &print, Print::StatusMonitor &status_monitor);

    LayerResult process_layer(
        const Print                     &print,
        Print::StatusMonitor            &status_monitor,
        // Set of object & print layers of the same PrintObject and with the same print_z.
        const std::vector<LayerToPrint> &layers,
        const LayerTools  				&layer_tools,
        const bool                       last_layer,
		// Pairs of PrintObject index and its instance index.
		const std::vector<const PrintInstance*> *ordering,
        // If set to size_t(-1), then print all copies of all objects.
        // Otherwise print a single copy of a single object.
        size_t                     single_object_idx = size_t(-1)
        );
    // Process all layers of all objects (non-sequential mode) with a parallel pipeline:
    // Generate G-code, run the filters (vase mode, cooling buffer), run the G-code analyser
    // and export G-code into file.
    void process_layers(
        const Print                                                         &print,
        Print::StatusMonitor                                                &status_monitor,
        const ToolOrdering                                                  &tool_ordering,
        const std::vector<const PrintInstance*>                             &print_object_instances_ordering,
        const std::vector<std::pair<coordf_t, std::vector<LayerToPrint>>>   &layers_to_print,
        std::string                                                         &preamble,
        GCodeOutputStream                                                   &output_stream);
    // Process all layers of a single object instance (sequential mode) with a parallel pipeline:
    // Generate G-code, run the filters (vase mode, cooling buffer), run the G-code analyser
    // and export G-code into file.
    void process_layers(
        const Print                             &print,
        Print::StatusMonitor                    &status_monitor,
        const ToolOrdering                      &tool_ordering,
        std::vector<LayerToPrint>                layers_to_print,
        const size_t                             single_object_idx,
        std::string                             &preamble,
        GCodeOutputStream                       &output_stream);

    void            set_last_pos(const Point &pos) { m_last_pos = pos; m_last_pos_defined = true; }
    bool            last_pos_defined() const { return m_last_pos_defined; }
    void            set_extruders(const std::vector<uint16_t> &extruder_ids);
    std::string     preamble();
    std::string     change_layer(coordf_t print_z);
    std::string     visitor_gcode;
    std::string     visitor_comment;
    double          visitor_speed;
    virtual void use(const ExtrusionPath &path) override { visitor_gcode += extrude_path(path, visitor_comment, visitor_speed); };
    virtual void use(const ExtrusionPath3D &path3D) override { visitor_gcode += extrude_path_3D(path3D, visitor_comment, visitor_speed); };
    virtual void use(const ExtrusionMultiPath &multipath) override { visitor_gcode += extrude_multi_path(multipath, visitor_comment, visitor_speed); };
    virtual void use(const ExtrusionMultiPath3D &multipath) override { visitor_gcode += extrude_multi_path3D(multipath, visitor_comment, visitor_speed); };
    virtual void use(const ExtrusionLoop &loop) override { visitor_gcode += extrude_loop(loop, visitor_comment, visitor_speed); };
    virtual void use(const ExtrusionEntityCollection &collection) override;
    std::string     extrude_entity(const ExtrusionEntity &entity, const std::string &description, double speed = -1.);
    std::string     extrude_loop(const ExtrusionLoop &loop, const std::string &description, double speed = -1.);
    std::string     extrude_loop_vase(const ExtrusionLoop &loop, const std::string &description, double speed = -1.);
    std::string     extrude_multi_path(const ExtrusionMultiPath &multipath, const std::string &description, double speed = -1.);
    std::string     extrude_multi_path3D(const ExtrusionMultiPath3D &multipath, const std::string &description, double speed = -1.);
    std::string     extrude_path(const ExtrusionPath &path, const std::string &description, double speed = -1.);
    std::string     extrude_path_3D(const ExtrusionPath3D &path, const std::string &description, double speed = -1.);
    void            split_at_seam_pos(ExtrusionLoop &loop, bool was_clockwise);
    template <typename THING = ExtrusionEntity> // can be templated safely because private
    void            add_wipe_points(const std::vector<THING>& paths);
    void            seam_notch(const ExtrusionLoop& original_loop, ExtrusionPaths& building_paths,
        ExtrusionPaths& notch_extrusion_start, ExtrusionPaths& notch_extrusion_end, bool is_hole_loop, bool is_full_loop_ccw);

    // Extruding multiple objects with soluble / non-soluble / combined supports
    // on a multi-material printer, trying to minimize tool switches.
    // Following structures sort extrusions by the extruder ID, by an order of objects and object islands.
    struct ObjectByExtruder
    {
        ObjectByExtruder() : support(nullptr), support_extrusion_role(erNone) {}
        const ExtrusionEntityCollection  *support;
        // erSupportMaterial / erSupportMaterialInterface or erMixed.
        ExtrusionRole                     support_extrusion_role;

        struct Island
        {
            struct Region {
            	// Non-owned references to LayerRegion::perimeters::entities()
            	// std::vector<const ExtrusionEntity*> would be better here, but there is no way in C++ to convert from std::vector<T*> std::vector<const T*> without copying.
                ExtrusionEntitiesPtr perimeters;
            	// Non-owned references to LayerRegion::fills::entities()
                ExtrusionEntitiesPtr infills;
                // Non-owned references to LayerRegion::ironing::entities()
                ExtrusionEntitiesPtr ironings;

                std::vector<const WipingExtrusions::ExtruderPerCopy*> infills_overrides;
                std::vector<const WipingExtrusions::ExtruderPerCopy*> perimeters_overrides;
                std::vector<const WipingExtrusions::ExtruderPerCopy*> ironings_overrides;

	            enum Type {
	            	PERIMETERS,
                    INFILL,
                    IRONING,
	            };

                // Appends perimeter/infill entities() and writes don't indices of those that are not to be extruder as part of perimeter/infill wiping
                void append(const Type type, const ExtrusionEntityCollection* eec, const WipingExtrusions::ExtruderPerCopy* copy_extruders);
            };


            std::vector<Region> by_region;                                    // all extrusions for this island, grouped by regions

            // Fills in by_region_per_copy_cache and returns its reference.
            const std::vector<Region>& by_region_per_copy(std::vector<Region> &by_region_per_copy_cache, unsigned int copy, uint16_t extruder, bool wiping_entities = false) const;
        };
        std::vector<Island>         islands;
    };

	struct InstanceToPrint
	{
        InstanceToPrint(ObjectByExtruder& object_by_extruder, size_t layer_id, const PrintObject& print_object, size_t instance_id) :
            object_by_extruder(object_by_extruder), layer_id(layer_id), print_object(print_object), instance_id(instance_id) {}

		// Repository 
		ObjectByExtruder		&object_by_extruder;
		// Index into std::vector<LayerToPrint>, which contains Object and Support layers for the current print_z, collected for a single object, or for possibly multiple objects with multiple instances.
		const size_t       		 layer_id;
		const PrintObject 		&print_object;
		// Instance idx of the copy of a print object.
		const size_t			 instance_id;
	};

	std::vector<InstanceToPrint> sort_print_object_instances(
		std::vector<ObjectByExtruder> 					&objects_by_extruder,
		// Object and Support layers for the current print_z, collected for a single object, or for possibly multiple objects with multiple instances.
		const std::vector<LayerToPrint> 				&layers,
		// Ordering must be defined for normal (non-sequential print).
		const std::vector<const PrintInstance*>     	*ordering,
		// For sequential print, the instance of the object to be printing has to be defined.
		const size_t                     				 single_object_instance_idx);

    std::string     extrude_perimeters(const Print &print, const std::vector<ObjectByExtruder::Island::Region> &by_region);
    std::string     extrude_infill(const Print& print, const std::vector<ObjectByExtruder::Island::Region>& by_region, bool is_infill_first);
    std::string     extrude_ironing(const Print& print, const std::vector<ObjectByExtruder::Island::Region>& by_region);
    std::string     extrude_support(const ExtrusionEntitiesPtr &support_fills);

    Polyline        travel_to(std::string& gcode, const Point &point, ExtrusionRole role);
    void            write_travel_to(std::string& gcode, const Polyline& travel, std::string comment);
    bool            can_cross_perimeter(const Polyline& travel, bool offset);
    bool            needs_retraction(const Polyline& travel, ExtrusionRole role = erNone, coordf_t max_min_dist = 0);
    std::string     retract(bool toolchange = false, bool inhibit_lift = false);
    std::string     unretract() { return m_writer.unlift() + m_writer.unretract(); }
    std::string     set_extruder(uint16_t extruder_id, double print_z, bool no_toolchange = false);
    std::string     toolchange(uint16_t extruder_id, double print_z);

    // Cache for custom seam enforcers/blockers for each layer.
    SeamPlacer                          m_seam_placer;
    bool                                m_seam_perimeters = false;

    /* Origin of print coordinates expressed in unscaled G-code coordinates.
       This affects the input arguments supplied to the extrude*() and travel_to()
       methods. */
    Vec2d                               m_origin;
    FullPrintConfig                     m_config;
    // scaled G-code resolution
    coordf_t                            m_scaled_gcode_resolution;
    GCodeWriter                         m_writer;
    PlaceholderParser                   m_placeholder_parser;
    // For random number generator etc.
    PlaceholderParser::ContextData      m_placeholder_parser_context;
    // Collection of templates, on which the placeholder substitution failed.
    std::map<std::string, std::string>  m_placeholder_parser_failed_templates;
    OozePrevention                      m_ooze_prevention;
    Wipe                                m_wipe;
    AvoidCrossingPerimeters             m_avoid_crossing_perimeters;
    bool                                m_enable_loop_clipping;
    // If enabled, the G-code generator will put following comments at the ends
    // of the G-code lines: _EXTRUDE_SET_SPEED, _WIPE, _BRIDGE_FAN_START, _BRIDGE_FAN_END, _BRIDGE_INTERNAL_FAN_START, _BRIDGE_INTERNAL_FAN_END
    // Those comments are received and consumed (removed from the G-code) by the CoolingBuffer.pm Perl module.
    bool                                m_enable_cooling_markers;
    // Markers for the Pressure Equalizer to recognize the extrusion type.
    // The Pressure Equalizer removes the markers from the final G-code.
    bool                                m_enable_extrusion_role_markers;
    // HACK to avoid multiple Z move.
    std::string                         m_delayed_layer_change;
    // Keeps track of the last extrusion role passed to the processor
    ExtrusionRole                       m_last_processor_extrusion_role;
    // How many times will change_layer() be called?
    // change_layer() will update the progress bar.
    uint32_t                            m_layer_count;
    // Progress bar indicator. Increments from -1 up to layer_count.
    int                                 m_layer_index;
    // Current layer processed. In sequential printing mode, only a single copy will be printed.
    // In non-sequential mode, all its copies will be printed.
    const Layer*                        m_layer;
    const PrintRegion*                  m_region = nullptr;
    // m_layer is an object layer and it is being printed over raft surface.
    bool                                m_object_layer_over_raft;    // idx of the current instance printed. (or the last one)
    uint16_t                            m_print_object_instance_id = -1;
    // For crossing perimeter retraction detection  (contain the layer & nozzle widdth used to construct it)
    // !!!! not thread-safe !!!! if threaded per layer, please store it in the thread.
    struct SliceOffsetted {
        std::vector<std::pair<ExPolygon, BoundingBox>> slices;
        std::vector<std::pair<ExPolygon, BoundingBox>> slices_offsetted;
        const Layer* layer;
        coord_t diameter;
    }                                   m_layer_slices_offseted{ {},{},nullptr, 0};
    double                              m_volumetric_speed;
    // Support for the extrusion role markers. Which marker is active?
    ExtrusionRole                       m_last_extrusion_role;
    // Not know the gapfill role for retract_lift_top
    ExtrusionRole                       m_last_notgapfill_extrusion_role;
    // Support for G-Code Processor
    float                               m_last_height{ 0.0f };
    float                               m_last_layer_z{ 0.0f };
    float                               m_max_layer_z{ 0.0f };
    float                               m_last_width{ 0.0f };
#if ENABLE_GCODE_VIEWER_DATA_CHECKING
    double                              m_last_mm3_per_mm;
#endif // ENABLE_GCODE_VIEWER_DATA_CHECKING

    Point                               m_last_pos;
    bool                                m_last_pos_defined;

    // a previous extrusion path that is too small to be extruded, have to fusion it into the next call.
    ExtrusionPath                       m_last_too_small;
    std::string                         m_last_description;
    double                              m_last_speed_mm_per_sec;

    std::unique_ptr<CoolingBuffer>      m_cooling_buffer;
    std::unique_ptr<SpiralVase>         m_spiral_vase;
    //to know the current spiral layer. Only for process_layer. began at 1, 0 means no spiral. Negative means disbaled spiral.
    int32_t                             m_spiral_vase_layer = 0;
    std::unique_ptr<GCodeFindReplace>   m_find_replace;
    std::unique_ptr<PressureEqualizer>  m_pressure_equalizer;
    std::unique_ptr<WipeTowerIntegration> m_wipe_tower;

    // Heights (print_z) at which the skirt has already been extruded.
    std::vector<coordf_t>               m_skirt_done;
    // Has the brim been extruded already? Brim is being extruded only for the first object of a multi-object print.
    bool                                m_brim_done;
    // Flag indicating whether the nozzle temperature changes from 1st to 2nd layer were performed.
    bool                                m_second_layer_things_done;
    // Index of a last object copy extruded.
    std::pair<const PrintObject*, Point> m_last_obj_copy;

    // ordered list of object, to give them a unique id.
    std::vector<const PrintObject*> m_ordered_objects;
    // gcode for the start/end of the current object block.
    // as the retraction/unretraction can be written after the start/end of the algoruihtmblock, it has to be delayed.
    std::string m_gcode_label_objects_start;
    std::string m_gcode_label_objects_end;
    void _add_object_change_labels(std::string &gcode);
    std::map<std::string, std::string> raw_str_to_objectid_str;

    bool m_silent_time_estimator_enabled;

    // Processor
    GCodeProcessor m_processor;

    //some post-processing on the file, with their data class
    std::unique_ptr<FanMover> m_fan_mover;

    std::function<void()> m_throw_if_canceled = [](){};

    std::string _extrude(const ExtrusionPath &path, const std::string &description, double speed = -1);
    void _extrude_line(std::string& gcode_str, const Line& line, const double e_per_mm, const std::string& comment);
    void _extrude_line_cut_corner(std::string& gcode_str, const Line& line, const double e_per_mm, const std::string& comment, Point& last_pos, const double path_width);
    std::string _before_extrude(const ExtrusionPath &path, const std::string &description, double speed = -1);
    double_t    _compute_speed_mm_per_sec(const ExtrusionPath& path, double speed = -1);
    std::string _after_extrude(const ExtrusionPath &path);
    void print_machine_envelope(GCodeOutputStream &file, const Print &print);
    void _print_first_layer_bed_temperature(std::string &out, const Print &print, const std::string &gcode, uint16_t first_printing_extruder_id, bool wait);
    void _print_first_layer_extruder_temperatures(std::string &out, const Print &print, const std::string &gcode, uint16_t first_printing_extruder_id, bool wait);
    // On the first printing layer. This flag triggers first layer speeds.
    bool                                on_first_layer() const { return m_layer != nullptr && m_layer->id() == 0; }
    // To control print speed of 1st object layer over raft interface.
    bool                                object_layer_over_raft() const { return m_object_layer_over_raft; }

    friend ObjectByExtruder& object_by_extruder(
        std::map<uint16_t, std::vector<ObjectByExtruder>>      &by_extruder, 
        uint16_t                                                extruder_id, 
        size_t                                                  object_idx, 
        size_t                                                  num_objects);
    friend std::vector<ObjectByExtruder::Island>& object_islands_by_extruder(
        std::map<uint16_t, std::vector<ObjectByExtruder>>      &by_extruder, 
        uint16_t                                                extruder_id, 
        size_t                                                  object_idx, 
        size_t                                                  num_objects,
        size_t                                                  num_islands);

    friend class Wipe;
    friend class WipeTowerIntegration;
    friend class PressureEqualizer;

    //utility for cooling markers
    static inline std::string _cooldown_marker_speed[ExtrusionRole::erCount];
    bool cooldwon_marker_no_slowdown_section = false;;
    static void cooldown_marker_init();
};

std::vector<const PrintInstance*> sort_object_instances_by_model_order(const Print& print);

}

#endif
