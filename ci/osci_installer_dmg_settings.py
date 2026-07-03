application = defines["app"]  # noqa: F821
volume_icon = defines["icon"]  # noqa: F821
app_filename = "osci-installer.app"
volume_icon_filename = ".VolumeIcon.icns"

files = [(application, app_filename)]
symlinks = {}
hide = [volume_icon_filename]
hide_extensions = [app_filename]

icon = volume_icon
format = "UDZO"

background = None
show_status_bar = False
show_tab_view = False
show_toolbar = False
show_pathbar = False
show_sidebar = False

window_rect = ((100, 100), (520, 520))
default_view = "icon-view"
show_icon_preview = False
include_icon_view_settings = True
include_list_view_settings = False

arrange_by = None
grid_offset = (0, 0)
grid_spacing = 100
scroll_position = (0, 0)
label_pos = "bottom"
text_size = 13
icon_size = 128
icon_locations = {
    app_filename: (260, 230),
    volume_icon_filename: (260, 900),
}
