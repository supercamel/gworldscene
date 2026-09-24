"""Check the public scalar-output contract in a freshly generated GIR."""
import sys
import xml.etree.ElementTree as ET

NS = {"gi": "http://www.gtk.org/introspection/core/1.0"}
C = "{http://www.gtk.org/introspection/c/1.0}"
OUTPUTS = {
    "gworld_scene_node_get_position": ["latitude", "longitude", "altitude_amsl"],
    "gworld_scene_node_get_orientation_ned": ["yaw_deg", "pitch_deg", "roll_deg"],
    "gworld_scene_node_get_scale": ["scale_x", "scale_y", "scale_z"],
    "gworld_scene_node_get_color": ["red", "green", "blue"],
    "gworld_scene_node_get_dimensions": ["width_m", "depth_m", "height_m"],
    "gworld_scene_cube_node_get_dimensions": ["width_m", "depth_m", "height_m"],
    "gworld_scene_cylinder_node_get_size": ["diameter_m", "height_m"],
    "gworld_scene_billboard_node_get_size_limits": ["min_px", "max_px"],
    "gworld_scene_billboard_node_get_reference_size": ["size_px", "distance_m"],
    "gworld_scene_ground_overlay_node_get_corners": [
        "top_left_latitude", "top_left_longitude", "top_right_latitude", "top_right_longitude",
        "bottom_right_latitude", "bottom_right_longitude", "bottom_left_latitude", "bottom_left_longitude",
    ],
    "gworld_scene_polygon_node_get_fill_color": ["red", "green", "blue", "alpha"],
    "gworld_scene_polygon_node_get_outline_color": ["red", "green", "blue", "alpha"],
    "gworld_scene_circle_node_get_fill_color": ["red", "green", "blue", "alpha"],
    "gworld_scene_circle_node_get_outline_color": ["red", "green", "blue", "alpha"],
    "gworld_scene_text_label_node_get_text_color": ["red", "green", "blue", "alpha"],
    "gworld_scene_text_label_node_get_background_color": ["red", "green", "blue", "alpha"],
    "gworld_scene_text_label_node_get_size_limits": ["min_px", "max_px"],
    "gworld_scene_text_label_node_get_reference_size": ["size_px", "distance_m"],
    "gworld_scene_view_get_camera": ["latitude", "longitude", "altitude_amsl"],
    "gworld_scene_view_get_camera_orientation": ["heading_deg", "pitch_deg"],
    "gworld_scene_view_get_free_camera_position": ["latitude", "longitude", "altitude_amsl"],
    "gworld_scene_view_get_free_camera_orientation": ["azimuth_deg", "pitch_deg"],
    "gworld_scene_view_get_sun_position": ["azimuth_deg", "elevation_deg"],
    "gworld_scene_view_get_fog_range": ["start_m", "end_m"],
    "gworld_scene_view_get_fog_color": ["red", "green", "blue"],
}


def check(path):
    root = ET.parse(path).getroot()
    methods = {method.get(C + "identifier"): method
               for method in root.findall(".//gi:method", NS)}
    for symbol, names in OUTPUTS.items():
        method = methods[symbol]
        assert method.get("introspectable", "1") == "1", symbol
        assert method.find("gi:return-value/gi:type", NS).get("name") == "none", symbol
        params = method.findall("gi:parameters/gi:parameter", NS)
        assert [p.get("name") for p in params] == names, symbol
        for param in params:
            label = symbol + "." + param.get("name")
            for key, value in {"direction": "out", "caller-allocates": "0",
                               "transfer-ownership": "none", "optional": "1"}.items():
                assert param.get(key) == value, (label, key, param.attrib)
            kind = param.find("gi:type", NS)
            assert kind.get("name") == "gdouble" and kind.get(C + "type") == "double*", label

    # This method has a boolean return, two inputs, and a required scalar output.
    sample = methods["gworld_scene_view_sample_terrain_altitude"]
    assert sample.find("gi:return-value/gi:type", NS).get("name") == "gboolean"
    params = sample.findall("gi:parameters/gi:parameter", NS)
    assert [p.get("name") for p in params] == ["latitude", "longitude", "altitude_amsl"]
    assert all(p.get("direction", "in") == "in" for p in params[:2])
    assert params[2].get("direction") == "out"
    assert params[2].get("caller-allocates") == "0"
    assert params[2].get("transfer-ownership") == "none"
    assert params[2].get("optional", "0") == "0"
    print(f"{path}: all {len(OUTPUTS)} getter contracts and terrain sampling passed")


for path in sys.argv[1:]:
    check(path)
